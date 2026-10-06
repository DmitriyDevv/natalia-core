/*
 * OBSERVE mode of the flight Algorithm on real hardware, without a PED.
 *
 * The super-loop is the one of main.c (without the watchdog); commands are pushed
 * as SystemEvents, so everything from handle_event() down to the NAND driver is the
 * flight code. The PED is replaced by TIM7: its interrupt (same priority as the PED
 * interrupts) puts events into the PED_REG ring through ped_reg_inject_event() at
 * OT_EVENT_RATE_HZ. Second marks come from RTC_OUT (PB2) as in flight; if none
 * arrives within OT_SECOND_WAIT_MS, TIM7 injects them once per second instead.
 *
 * Every injected event carries a sequence number: t_trig = low word, t_pe_dead =
 * high word, amp_d and trig_stat are derived from it. When the ring is full the
 * event is held and offered again on the next tick, as the PED would wait.
 *
 * Sequence:
 *   1. ERASE bank OT_BANK_ID (packet counter in MRAM back to 0).
 *   2. OBSERVE_START (PED power off, registration on, OT_PARAMS), then one KT 0E00h
 *      (McIlwain): with NATALIA_OBSERVE_EVENTS_WAIT_KT the Events format waits for
 *      a KT. Events are injected for OT_DURATION_S seconds, progress once per second.
 *   3. CMD_DUTY: the mode waits for the next second mark, writes the final
 *      Counters / Telemetry, pads the last packet with AAAAh and returns to DUTY.
 *   4. Verification by reading the bank back: packet markers, session, numbers,
 *      CRC chain and core CRC; every format header (mark, class, number, header
 *      CRC); the Events formats must hold one gap-free run of sequence numbers.
 *   5. DUMP of the written packets over FTDI (OT_DO_DUMP); the PC side checks the
 *      same with: python3 tools/ni_dump_receiver.py --expect N --formats --events-seq
 *      (start it any time before, it waits for the FTDI port).
 *
 * DESTRUCTIVE for the selected bank.
 *
 * Compile-time knobs (-DCMAKE_C_FLAGS="-D..."):
 *   OT_BANK_ID        1 or 2                                  (default 1)
 *   OT_PARAMS         observe parameters word                 (default 0x0009: Events always, Nmax = 1)
 *   OT_EVENT_RATE_HZ  injected events per second              (default 10000)
 *   OT_DURATION_S     seconds of observation                  (default 30)
 *   OT_DO_DUMP        0/1 DUMP the session over FTDI           (default 1)
 *   OT_KT_DELAY_MS    KT 0E00h this long after the start      (default 0 = at once);
 *                     > 0: the check also requires one McIlwain format and no
 *                     event older than the KT in the Events formats
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/observe_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON -DNATALIA_ENABLE_PED_REG_DRIVER=ON
 *   -DNATALIA_ENABLE_PED_INJECT=ON -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarm.h"
#include "alarm_monitor.h"
#include "algorithm.h"
#include "board_api.h"
#include "clock.h"
#include "crc16.h"
#include "debug_log.h"
#include "dump_mode_config.h"
#include "natalia_build_info.h"
#include "event_queue.h"
#include "nand_map.h"
#include "ni_format.h"
#include "ni_stream.h"
#include "ped_reg.h"
#include "state.h"
#include "stm32l4xx.h"
#include "test_mode_config.h"
#include "timebase.h"
#include "tlm_staging.h"
#include "transport.h"

#if !defined(NATALIA_PED_REG_INJECT) || (NATALIA_PED_REG_INJECT == 0)
#error "observe_test needs -DNATALIA_ENABLE_PED_INJECT=ON"
#endif

#ifndef OT_BANK_ID
#define OT_BANK_ID 1U
#endif

#ifndef OT_PARAMS
#define OT_PARAMS 0x0009U
#endif

#ifndef OT_EVENT_RATE_HZ
#define OT_EVENT_RATE_HZ 10000UL
#endif

#ifndef OT_DURATION_S
#define OT_DURATION_S 30UL
#endif

#ifndef OT_DO_DUMP
#define OT_DO_DUMP 1
#endif

#ifndef OT_KT_DELAY_MS
#define OT_KT_DELAY_MS 0UL
#endif

#define OT_START_DELAY_MS  5000UL
#define OT_DUMP_WAIT_MS    10000UL
#define OT_DUMP_TIMEOUT_MS 600000UL
#define OT_SECOND_WAIT_MS  2500UL
#define OT_ERASE_TIMEOUT_MS 60000UL
#define OT_FINISH_TIMEOUT_MS 10000UL
#define OT_TRIGGER_CONFIG  0x0000U
#define OT_TIM_IRQ_PRIORITY 0U

#define OT_MAX_FORMAT_TYPES 8U

#if (OT_BANK_ID == 1)
#define OT_NAND_BANK NAND_BANK_1
#else
#define OT_NAND_BANK NAND_BANK_2
#endif

void TIM7_IRQHandler(void);

static SystemContext ot_ctx;
static SystemState ot_last_state;
static uint8_t ot_packet[NI_PACKET_BYTES];

static volatile uint8_t ot_inject_events;
static volatile uint8_t ot_inject_seconds;
static volatile uint32_t ot_seq;
static volatile uint32_t ot_held_ticks;
static volatile uint32_t ot_tick;

static uint32_t ot_loop_max_us;
static uint32_t ot_log_last_us;
static uint32_t ot_log_max_us;
static uint32_t ot_log_ring_growth_max;
static uint32_t ot_kt_seq;

/* ------------------------------------------------------------------------- */
/* Log                                                                        */
/* ------------------------------------------------------------------------- */

static void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void log_line(const char* format, ...) {
    char text[160];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    debug_log_write(text);
}

static const char* state_name(SystemState state) {
    switch (state) {
    case STATE_INIT:
        return "INIT";
    case STATE_DUTY:
        return "DUTY";
    case STATE_ERASE:
        return "ERASE";
    case STATE_TEST:
        return "TEST";
    case STATE_OBSERVE:
        return "OBSERVE";
    case STATE_DUMP:
        return "DUMP";
    case STATE_ALARM:
        return "ALARM";
    case STATE_SHUTDOWN:
        return "SHUTDOWN";
    default:
        return "UNKNOWN";
    }
}

static void halt(const char* reason) {
    ot_inject_events = 0U;
    ot_inject_seconds = 0U;
    log_line("STOP: %s\r\n", reason);
    debug_log_write("===== OBSERVE TEST FAILED =====\r\n");

    for (;;) {
    }
}

/* ------------------------------------------------------------------------- */
/* Event generator (TIM7)                                                     */
/* ------------------------------------------------------------------------- */

static uint16_t ot_amp(uint32_t seq) {
    return (uint16_t)(((seq * 2654435761UL) >> 16) & 0xFFFFU);
}

static uint16_t ot_trig(uint32_t seq) {
    return (uint16_t)(0x5A00U | (seq & 0xFFU));
}

void TIM7_IRQHandler(void) {
    uint16_t words[4];

    TIM7->SR = 0U;

    if (ot_inject_events == 0U) {
        return;
    }

    ++ot_tick;
    if ((ot_inject_seconds != 0U) && ((ot_tick % OT_EVENT_RATE_HZ) == 0U)) {
        (void)ped_reg_inject_second();
    }

    words[0] = (uint16_t)(ot_seq & 0xFFFFU);
    words[1] = (uint16_t)(ot_seq >> 16);
    words[2] = ot_amp(ot_seq);
    words[3] = ot_trig(ot_seq);

    if (ped_reg_inject_event(words) == BOARD_OK) {
        ++ot_seq;
    } else {
        ++ot_held_ticks;
    }
}

static void generator_init(void) {
    uint32_t timer_hz = clock_get_pclk1_hz();

    if ((RCC->CFGR & RCC_CFGR_PPRE1_2) != 0UL) {
        timer_hz *= 2UL;
    }

    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM7EN;
    (void)RCC->APB1ENR1;

    TIM7->CR1 = 0U;
    TIM7->PSC = 0U;
    TIM7->ARR = (timer_hz / OT_EVENT_RATE_HZ) - 1UL;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0U;
    TIM7->DIER = TIM_DIER_UIE;

    NVIC_SetPriority(TIM7_IRQn, OT_TIM_IRQ_PRIORITY);
    NVIC_EnableIRQ(TIM7_IRQn);

    TIM7->CR1 = TIM_CR1_CEN;

    log_line("generator: %lu events/s, timer %lu Hz, ARR %lu\r\n",
             (unsigned long)OT_EVENT_RATE_HZ, (unsigned long)timer_hz, (unsigned long)TIM7->ARR);
}

/* ------------------------------------------------------------------------- */
/* Super-loop (as main.c, without the watchdog)                               */
/* ------------------------------------------------------------------------- */

static void init_system_context(SystemContext* ctx) {
    (void)memset(ctx, 0, sizeof(*ctx));

    ctx->state = STATE_INIT;
    ctx->previous_state = STATE_INIT;
    ctx->alarm_mask = ALARM_ALL_MASK;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

static void super_loop_step(void) {
    uint32_t start = timebase_cycles();
    uint32_t now_ms = timebase_millis();
    uint32_t elapsed_us;

    (void)transport_poll(&ot_ctx, now_ms);
    algorithm_collect_hw_events(&ot_ctx);
    alarm_monitor_poll(&ot_ctx, now_ms);
    algorithm_poll(&ot_ctx);
    algorithm_process_events(&ot_ctx);

    elapsed_us = timebase_us_since(start);
    if (elapsed_us > ot_loop_max_us) {
        ot_loop_max_us = elapsed_us;
    }

    if (ot_ctx.state != ot_last_state) {
        log_line("STATE %s -> %s alarm=0x%04lX masked=0x%04lX\r\n",
                 state_name(ot_last_state), state_name(ot_ctx.state),
                 (unsigned long)ot_ctx.alarm_status, (unsigned long)ot_ctx.masked_alarm);
        ot_last_state = ot_ctx.state;
    }
}

static void push_event(const SystemEvent* event) {
    if (!system_event_queue_push_back(event)) {
        halt("event queue full");
    }
}

/* ------------------------------------------------------------------------- */
/* Steps                                                                      */
/* ------------------------------------------------------------------------- */

static void erase_bank(void) {
    SystemEvent event;
    uint32_t start_ms = timebase_millis();

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_ERASE;
    event.command.erase.bank = OT_NAND_BANK;
    event.command.erase.power_after_done = POWER_AFTER_DONE_OFF;
    push_event(&event);

    do {
        super_loop_step();
        if (timebase_elapsed(start_ms, OT_ERASE_TIMEOUT_MS)) {
            halt("erase timeout");
        }
    } while ((ot_ctx.state == STATE_ERASE) || (system_event_queue_get_count() != 0U));

    if (ot_ctx.state != STATE_DUTY) {
        halt("erase did not end in DUTY");
    }

    log_line("ERASE: done, %lu ms\r\n", (unsigned long)(timebase_millis() - start_ms));
}

static void log_progress(uint32_t second) {
    BoardPedStats stats;

    (void)board_ped_get_stats(&stats);

    log_line("  t=%lus pkt=%lu inj=%lu held=%lu sec=%lu ring_max=%lu full_pass=%lu loop_max=%luus"
             " log=%lu/%luus log_ring+=%lu\r\n",
             (unsigned long)second,
             (unsigned long)ot_ctx.observe.committed_packet_count,
             (unsigned long)ot_seq,
             (unsigned long)ot_held_ticks,
             (unsigned long)stats.seconds_marked,
             (unsigned long)stats.ring_high_water,
             (unsigned long)ot_ctx.observe.stats.records_held,
             (unsigned long)ot_loop_max_us,
             (unsigned long)ot_log_last_us,
             (unsigned long)ot_log_max_us,
             (unsigned long)ot_log_ring_growth_max);
}

/* Prints the progress line and measures how long the CAN log blocks the loop and
 * how much the PED ring grows meanwhile (shown in the next line). */
static void log_progress_timed(uint32_t second) {
    BoardPedStats before;
    BoardPedStats after;
    uint32_t start;
    uint32_t growth;

    (void)board_ped_get_stats(&before);
    start = timebase_cycles();

    log_progress(second);

    ot_log_last_us = timebase_us_since(start);
    (void)board_ped_get_stats(&after);

    if (ot_log_last_us > ot_log_max_us) {
        ot_log_max_us = ot_log_last_us;
    }

    growth = (after.ring_count > before.ring_count) ? (after.ring_count - before.ring_count) : 0U;
    if (growth > ot_log_ring_growth_max) {
        ot_log_ring_growth_max = growth;
    }
}

/* One KT 0E00h (McIlwain, zero payload); remembers the next event number. */
static void push_kt(void) {
    static const uint8_t mcilwain[NI_KT_MCILWAIN_BYTES] = {0};
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_MCILWAIN;
    event.tlm_slot = tlm_staging_put(mcilwain, (uint16_t)sizeof(mcilwain));
    ot_kt_seq = ot_seq;
    push_event(&event);
    log_line("OBSERVE: KT 0E00h (McIlwain) pushed at event %lu, events wait for a KT: %u\r\n",
             (unsigned long)ot_kt_seq, (unsigned)ot_ctx.observe.events_wait_kt);
}

static void observe_run(void) {
    bool kt_pushed = false;
    SystemEvent event;
    BoardPedStats stats;
    uint32_t active_ms = 0U;
    uint32_t last_log_ms;
    uint32_t second = 0U;
    uint32_t start_ms;

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_OBSERVE_START;
    event.command.observe_start.bank = OT_NAND_BANK;
    event.command.observe_start.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.observe_start.observe_params = OT_PARAMS;
    event.command.observe_start.trigger_config = OT_TRIGGER_CONFIG;
    event.command.observe_start.ped_power_enabled = false;
    event.command.observe_start.registration_enabled = true;
    push_event(&event);

    log_line("OBSERVE: start, params 0x%04X, %lu s\r\n", (unsigned)OT_PARAMS, (unsigned long)OT_DURATION_S);

    start_ms = timebase_millis();
    while (ot_ctx.observe.stage != OBSERVE_STAGE_ACTIVE) {
        super_loop_step();
        if ((ot_ctx.state != STATE_OBSERVE) && (system_event_queue_get_count() == 0U)) {
            halt("OBSERVE not entered");
        }
        if (timebase_elapsed(start_ms, OT_FINISH_TIMEOUT_MS)) {
            halt("OBSERVE did not become active");
        }
    }

    log_line("OBSERVE: active after %lu ms, session %u, first packet %lu\r\n",
             (unsigned long)(timebase_millis() - start_ms), (unsigned)ot_ctx.observe_session_id,
             (unsigned long)ot_ctx.observe.committed_packet_count);

    if (OT_KT_DELAY_MS == 0UL) {
        push_kt();
    }

    ot_loop_max_us = 0U;
    ot_inject_events = 1U;
    active_ms = timebase_millis();
    last_log_ms = active_ms;

    while (second < OT_DURATION_S) {
        super_loop_step();

        if ((OT_KT_DELAY_MS != 0UL) && !kt_pushed && timebase_elapsed(active_ms, OT_KT_DELAY_MS)) {
            kt_pushed = true;
            push_kt();
        }

        if (ot_ctx.state != STATE_OBSERVE) {
            halt("left OBSERVE during observation");
        }

        if ((ot_inject_seconds == 0U) && timebase_elapsed(active_ms, OT_SECOND_WAIT_MS)) {
            (void)board_ped_get_stats(&stats);
            if (stats.seconds_marked == 0U) {
                ot_inject_seconds = 1U;
                log_line("OBSERVE: no second marks from RTC_OUT, TIM7 injects them\r\n");
            }
        }

        if (timebase_elapsed(last_log_ms, 1000UL)) {
            last_log_ms += 1000UL;
            ++second;
            log_progress_timed(second);
        }
    }

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUTY;
    event.command.duty.bank = OT_NAND_BANK;
    event.command.duty.power_after_done = POWER_AFTER_DONE_OFF;
    push_event(&event);
    log_line("OBSERVE: CMD_DUTY\r\n");

    start_ms = timebase_millis();
    while ((ot_ctx.state == STATE_OBSERVE) || (system_event_queue_get_count() != 0U)) {
        super_loop_step();
        if (timebase_elapsed(start_ms, OT_FINISH_TIMEOUT_MS)) {
            halt("OBSERVE did not finish");
        }
    }

    ot_inject_events = 0U;
    ot_inject_seconds = 0U;

    (void)board_ped_get_stats(&stats);
    log_line("OBSERVE: finished in %lu ms, state %s, alarm=0x%04lX, stage %u\r\n",
             (unsigned long)(timebase_millis() - start_ms), state_name(ot_ctx.state),
             (unsigned long)ot_ctx.alarm_status, (unsigned)ot_ctx.observe.stage);
    log_line("  log: max %lu us per progress line, ring growth during a line max %lu\r\n",
             (unsigned long)ot_log_max_us, (unsigned long)ot_log_ring_growth_max);
    log_line("  packets=%lu injected=%lu held_ticks=%lu ring_max=%lu seconds=%lu loop_max=%luus\r\n",
             (unsigned long)ot_ctx.observe.committed_packet_count, (unsigned long)ot_seq,
             (unsigned long)ot_held_ticks, (unsigned long)stats.ring_high_water,
             (unsigned long)stats.seconds_marked, (unsigned long)ot_loop_max_us);
    log_line("  taken: records=%lu events=%lu seconds=%lu, stream-full passes=%lu\r\n",
             (unsigned long)ot_ctx.observe.stats.records_taken,
             (unsigned long)ot_ctx.observe.stats.events_taken,
             (unsigned long)ot_ctx.observe.stats.seconds_taken,
             (unsigned long)ot_ctx.observe.stats.records_held);

    if ((ot_ctx.state != STATE_DUTY) || (ot_ctx.observe.stage != OBSERVE_STAGE_EXIT_CMD)) {
        halt("OBSERVE did not end in DUTY");
    }
}

/* ------------------------------------------------------------------------- */
/* Verification                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint16_t header[NI_FORMAT_HEADER_WORDS];
    uint32_t header_fill;
    uint32_t remaining;
    uint8_t type;
    uint16_t event[NI_FORMAT_EVENT_WORDS];
    uint32_t event_fill;
    bool padded;
    uint32_t next_number;
    uint32_t types[OT_MAX_FORMAT_TYPES];
    uint32_t events;
    bool have_seq;
    uint32_t first_seq;
    uint32_t last_seq;
    uint32_t gaps;
    uint32_t bad_event;
    uint32_t bad_header;
    uint32_t bad_pad;
} FormatParser;

static FormatParser ot_parser;

static void parser_event(FormatParser* parser) {
    uint32_t seq = (uint32_t)parser->event[0] | ((uint32_t)parser->event[1] << 16);

    if ((parser->event[2] != ot_amp(seq)) || (parser->event[3] != ot_trig(seq))) {
        ++parser->bad_event;
    }

    if (!parser->have_seq) {
        parser->have_seq = true;
        parser->first_seq = seq;
    } else if (seq != (parser->last_seq + 1U)) {
        ++parser->gaps;
    }

    parser->last_seq = seq;
    ++parser->events;
}

static void parser_header(FormatParser* parser) {
    uint8_t bytes[(NI_FORMAT_HEADER_WORDS - 1U) * 2U];
    uint32_t number = (uint32_t)parser->header[3] | ((uint32_t)parser->header[4] << 16);
    uint16_t length = parser->header[7];
    uint32_t i;

    for (i = 0U; i < (NI_FORMAT_HEADER_WORDS - 1U); ++i) {
        bytes[i * 2U] = (uint8_t)(parser->header[i] & 0xFFU);
        bytes[(i * 2U) + 1U] = (uint8_t)(parser->header[i] >> 8);
    }

    parser->type = (uint8_t)(parser->header[1] & 0xFFU);

    if ((parser->header[0] != NI_FORMAT_MARK) || ((parser->header[1] >> 8) != NI_FORMAT_CLASS_CODE) ||
        (parser->type >= OT_MAX_FORMAT_TYPES) || (number != parser->next_number) ||
        (length <= NI_FORMAT_HEADER_WORDS) ||
        (crc16_ccitt(bytes, sizeof(bytes)) != parser->header[8])) {
        ++parser->bad_header;
    }

    if (parser->type < OT_MAX_FORMAT_TYPES) {
        ++parser->types[parser->type];
    }

    parser->next_number = number + 1U;
    parser->remaining = (length > NI_FORMAT_HEADER_WORDS) ? (uint32_t)(length - NI_FORMAT_HEADER_WORDS) : 1U;
    parser->event_fill = 0U;
}

static void parser_feed(FormatParser* parser, uint16_t word) {
    if (parser->padded) {
        if (word != NI_PACKET_PAD_WORD) {
            ++parser->bad_pad;
        }
        return;
    }

    if (parser->header_fill < NI_FORMAT_HEADER_WORDS) {
        if ((parser->header_fill == 0U) && (word == NI_PACKET_PAD_WORD)) {
            parser->padded = true;
            return;
        }

        parser->header[parser->header_fill] = word;
        ++parser->header_fill;
        if (parser->header_fill == NI_FORMAT_HEADER_WORDS) {
            parser_header(parser);
        }
        return;
    }

    if ((parser->remaining > 1U) && (parser->type == (uint8_t)NI_FORMAT_EVENTS)) {
        parser->event[parser->event_fill] = word;
        ++parser->event_fill;
        if (parser->event_fill == NI_FORMAT_EVENT_WORDS) {
            parser_event(parser);
            parser->event_fill = 0U;
        }
    }

    --parser->remaining;
    if (parser->remaining == 0U) {
        parser->header_fill = 0U;
    }
}

static uint16_t packet_word(uint32_t index) {
    return (uint16_t)((uint16_t)ot_packet[index * 2U] | ((uint16_t)ot_packet[(index * 2U) + 1U] << 8));
}

static uint32_t verify_bank(uint32_t packets, uint16_t session) {
    FormatParser* parser = &ot_parser;
    uint32_t failures = 0U;
    uint32_t bad_packets = 0U;
    uint16_t previous_crc = 0U;
    uint8_t is_powered = 0U;
    uint32_t index;
    uint32_t i;
    BoardStatus status;

    (void)memset(parser, 0, sizeof(*parser));

    if ((board_nand_power_on((uint8_t)OT_BANK_ID) != BOARD_OK) ||
        (board_nand_is_powered((uint8_t)OT_BANK_ID, &is_powered) != BOARD_OK) || (is_powered == 0U) ||
        (board_nand_connect((uint8_t)OT_BANK_ID) != BOARD_OK) ||
        (nand_map_load((uint8_t)OT_BANK_ID) != BOARD_OK) ||
        (board_nand_open_read((uint8_t)OT_BANK_ID, packets) != BOARD_OK)) {
        halt("bank open for verification");
    }

    for (index = 0U; index < packets; ++index) {
        status = board_nand_read_packet((uint8_t)OT_BANK_ID, index, ot_packet);
        if (status != BOARD_OK) {
            log_line("  packet %lu: read status %d\r\n", (unsigned long)index, (int)status);
            ++bad_packets;
            continue;
        }

        if ((packet_word(0U) != NI_PACKET_MARKER_1) || (packet_word(1U) != NI_PACKET_MARKER_2) ||
            (packet_word(2U) != NI_PACKET_MARKER_3) || (packet_word(3U) != session) ||
            (((uint32_t)packet_word(4U) | ((uint32_t)packet_word(5U) << 16)) != index) ||
            (packet_word(6U) != previous_crc) ||
            (crc16_ccitt(&ot_packet[NI_PACKET_HEADER_WORDS * 2U], NI_PACKET_CORE_WORDS * 2U) !=
             packet_word(NI_PACKET_WORDS - 1U))) {
            if (bad_packets < 4U) {
                log_line("  packet %lu: bad header or CRC\r\n", (unsigned long)index);
            }
            ++bad_packets;
        }

        previous_crc = packet_word(NI_PACKET_WORDS - 1U);

        for (i = 0U; i < NI_PACKET_CORE_WORDS; ++i) {
            parser_feed(parser, packet_word(NI_PACKET_HEADER_WORDS + i));
        }
    }

    (void)board_nand_disconnect((uint8_t)OT_BANK_ID);
    (void)board_nand_power_off((uint8_t)OT_BANK_ID);

    log_line("VERIFY: %lu packets, bad %lu, formats %lu, bad headers %lu, padded %u, bad pad %lu\r\n",
             (unsigned long)packets, (unsigned long)bad_packets, (unsigned long)parser->next_number,
             (unsigned long)parser->bad_header, (unsigned)parser->padded, (unsigned long)parser->bad_pad);
    log_line("  types: 00=%lu 01=%lu 02=%lu 03=%lu 04=%lu 05=%lu 06=%lu 07=%lu\r\n",
             (unsigned long)parser->types[0], (unsigned long)parser->types[1],
             (unsigned long)parser->types[2], (unsigned long)parser->types[3],
             (unsigned long)parser->types[4], (unsigned long)parser->types[5],
             (unsigned long)parser->types[6], (unsigned long)parser->types[7]);
    log_line("  events %lu, seq %lu..%lu, gaps %lu, bad %lu\r\n",
             (unsigned long)parser->events, (unsigned long)parser->first_seq,
             (unsigned long)parser->last_seq, (unsigned long)parser->gaps,
             (unsigned long)parser->bad_event);

    if ((bad_packets != 0U) || (parser->bad_header != 0U) || !parser->padded || (parser->bad_pad != 0U)) {
        ++failures;
    }
    if ((parser->gaps != 0U) || (parser->bad_event != 0U)) {
        ++failures;
    }
    if ((OT_KT_DELAY_MS != 0UL) &&
        ((parser->types[NI_FORMAT_MCILWAIN] != 1U) || !parser->have_seq || (parser->first_seq < ot_kt_seq))) {
        log_line("  KT check FAILED: McIlwain formats %lu, first event %lu, KT at event %lu\r\n",
                 (unsigned long)parser->types[NI_FORMAT_MCILWAIN], (unsigned long)parser->first_seq,
                 (unsigned long)ot_kt_seq);
        ++failures;
    } else if (OT_KT_DELAY_MS != 0UL) {
        log_line("  KT check: McIlwain formats 1, first event %lu >= KT at event %lu (+%lu)\r\n",
                 (unsigned long)parser->first_seq, (unsigned long)ot_kt_seq,
                 (unsigned long)(parser->first_seq - ot_kt_seq));
    }
    if (parser->have_seq && (parser->events != ((parser->last_seq - parser->first_seq) + 1U))) {
        ++failures;
    }
    if ((parser->types[NI_FORMAT_TELEMETRY] == 0U) || (parser->types[NI_FORMAT_COUNTERS] == 0U)) {
        ++failures;
    }

    return failures;
}

/* ------------------------------------------------------------------------- */
/* DUMP                                                                       */
/* ------------------------------------------------------------------------- */

#if (OT_DO_DUMP != 0)
static uint32_t dump_session(uint32_t packets) {
    SystemEvent event;
    uint32_t start_ms;
    uint32_t last_log_ms;

    log_line("DUMP: %lu packets; PC: python3 tools/ni_dump_receiver.py --expect %lu --formats --events-seq\r\n",
             (unsigned long)packets, (unsigned long)packets);
    log_line("DUMP: starts in %lu ms\r\n", (unsigned long)OT_DUMP_WAIT_MS);
    timebase_delay_ms_blocking(OT_DUMP_WAIT_MS);

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.command.dump.bank = OT_NAND_BANK;
    event.command.dump.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.dump.start_address = 0U;
    event.command.dump.requested_packet_count = packets;
    event.command.dump.size = packets * DUMP_MODE_PACKET_SIZE;
    event.command.dump.dump_all = false;
    push_event(&event);

    start_ms = timebase_millis();
    last_log_ms = start_ms;

    do {
        super_loop_step();

        if (timebase_elapsed(last_log_ms, 5000UL)) {
            last_log_ms += 5000UL;
            log_line("  dump bytes=%lu/%lu\r\n", (unsigned long)ot_ctx.dump.bytes_done,
                     (unsigned long)ot_ctx.dump.size);
        }

        if (timebase_elapsed(start_ms, OT_DUMP_TIMEOUT_MS)) {
            halt("DUMP timeout");
        }
    } while ((ot_ctx.state == STATE_DUMP) || (system_event_queue_get_count() != 0U));

    log_line("DUMP: %lu ms, state %s, alarm=0x%04lX, bytes %lu/%lu, failed=%u\r\n",
             (unsigned long)(timebase_millis() - start_ms), state_name(ot_ctx.state),
             (unsigned long)ot_ctx.alarm_status, (unsigned long)ot_ctx.dump.bytes_done,
             (unsigned long)ot_ctx.dump.size, (unsigned)ot_ctx.dump.operation_failed);

    return ((ot_ctx.state == STATE_DUTY) && (ot_ctx.dump.bytes_done == ot_ctx.dump.size)) ? 0U : 1U;
}
#endif

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */

int main(void) {
    uint32_t failures;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(OT_START_DELAY_MS);
    log_line("\r\n===== OBSERVE TEST (flight Algorithm, PED events from TIM7), bank %u =====\r\n",
             (unsigned)OT_BANK_ID);
    log_line("build %s\r\n", NATALIA_BUILD_TIME);

    init_system_context(&ot_ctx);
    system_event_queue_init();
    ot_last_state = ot_ctx.state;

    (void)system_event_queue_push_back_type(EVENT_BOOT);
    algorithm_process_events(&ot_ctx);
    (void)system_event_queue_push_back_type((board_comm_init() == BOARD_OK) ? EVENT_INIT_DONE : EVENT_INIT_FAIL);
    algorithm_process_events(&ot_ctx);
    super_loop_step();

    if (ot_ctx.state != STATE_DUTY) {
        halt("FSM did not reach DUTY after boot");
    }

    generator_init();
    erase_bank();
    observe_run();

    failures = verify_bank(ot_ctx.observe.committed_packet_count, ot_ctx.observe_session_id);
#if (OT_DO_DUMP != 0)
    failures += dump_session(ot_ctx.observe.committed_packet_count);
#endif

    log_line("failures=%lu\r\n", (unsigned long)failures);
    debug_log_write((failures == 0U) ? "===== OBSERVE TEST PASSED =====\r\n"
                                     : "===== OBSERVE TEST FAILED =====\r\n");

    for (;;) {
    }

    return 0;
}
