/*
 * NAND modes of the flight Algorithm on real hardware: DUMP, ERASE, TEST.
 *
 * The super-loop is the one of main.c (transport_poll, algorithm_collect_hw_events,
 * alarm_monitor_poll, algorithm_poll, algorithm_process_events). Commands are not
 * received over CAN: the same SystemEvent that transport builds for a command is
 * pushed into the event queue, so everything from handle_event() down to the
 * driver is the flight code. Every mode is started with power_after_done = OFF,
 * as on board, so each mode also powers the bank on and off.
 *
 * Sequence (each step can be disabled):
 *   1. FILL  - via Board_API: erase the bank, write MT_DUMP_PACKETS NI packets
 *              (format "Формат научной информации ГС v6", same generator as
 *              ni_fill_test), bank off.
 *   2. DUMP  - EVENT_CMD_DUMP for MT_DUMP_PACKETS packets. The stream goes out over
 *              FTDI; check it on the PC with tools/ni_dump_receiver.py --expect N.
 *              FTDI is powered only during DUMP: the receiver waits for the port.
 *   3. ERASE - EVENT_CMD_ERASE; afterwards an independent read check: the first
 *              packet of every block must be 0xFF (blocks 0..MT_DUMP_PACKETS/128
 *              held data before the erase).
 *   4. TEST  - EVENT_CMD_TEST on the whole bank; its own NERR / status are printed.
 *   5. ALARM RECOVERY - raise ALARM_NAND_PR (non-maskable): the FSM must
 *              enter ALARM (safe configuration); EVENT_CMD_RESET_ALARM must bring it
 *              back to DUTY; then ERASE must work again (it needs MRAM, which the
 *              safe configuration must leave usable).
 *
 * A mode is finished when the FSM leaves the mode state: DUTY = success,
 * ALARM = failure (alarm bits are printed).
 *
 * DESTRUCTIVE for the selected bank.
 *
 * Compile-time knobs (-DCMAKE_C_FLAGS="-D..."):
 *   MT_BANK_ID        1 or 2                         (default 1)
 *   MT_DUMP_PACKETS   packets written and dumped     (default 8192 = 16 MiB)
 *   MT_DO_FILL / MT_DO_DUMP / MT_DO_ERASE / MT_DO_TEST / MT_DO_ALARM_RECOVERY  0/1 (default 1)
 *   MT_DUMP_WAIT_MS   pause before DUMP to start the PC receiver (default 15000)
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/nand_modes_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON -DNATALIA_ENABLE_FTDI_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdarg.h>
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
#include "event_queue.h"
#include "nand_map.h"
#include "state.h"
#include "test_mode_config.h"
#include "timebase.h"
#include "transport.h"

#ifndef MT_BANK_ID
#define MT_BANK_ID 1U
#endif

#ifndef MT_DUMP_PACKETS
#define MT_DUMP_PACKETS 8192UL
#endif

#ifndef MT_DO_FILL
#define MT_DO_FILL 1
#endif

#ifndef MT_DO_DUMP
#define MT_DO_DUMP 1
#endif

#ifndef MT_DO_ERASE
#define MT_DO_ERASE 1
#endif

#ifndef MT_DO_TEST
#define MT_DO_TEST 1
#endif

#ifndef MT_DO_ALARM_RECOVERY
#define MT_DO_ALARM_RECOVERY 1
#endif

#ifndef MT_DUMP_WAIT_MS
#define MT_DUMP_WAIT_MS 15000UL
#endif

#define MT_START_DELAY_MS 5000UL
#define MT_PROGRESS_MS 10000UL
#define MT_MODE_TIMEOUT_MS (90UL * 60UL * 1000UL)
#define MT_NI_SESSION_ID 7U

#if (MT_BANK_ID == 1U)
#define MT_NAND_BANK NAND_BANK_1
#elif (MT_BANK_ID == 2U)
#define MT_NAND_BANK NAND_BANK_2
#else
#error "MT_BANK_ID must be 1 or 2"
#endif

#define NI_PACKET_WORDS 1024U
#define NI_PACKET_BYTES (NI_PACKET_WORDS * 2U)
#define NI_HEADER_WORDS 7U
#define NI_CORE_WORDS (NI_PACKET_WORDS - NI_HEADER_WORDS - 1U)
#define NI_MARKER_1 0x46FFU
#define NI_MARKER_2 0xC9D7U
#define NI_MARKER_3 0xA5B3U
#define NI_PAD_WORD 0xAAAAU
#define NI_FORMAT_MARK_1 0xFEFAU
#define NI_FORMAT_MARK_2 0x000FU
#define NI_FORMAT_COUNTERS 0x01U
#define NI_OBSERVE_MODE 0x01U
#define NI_COUNTERS_WORDS 15U

#if ((MT_DUMP_PACKETS % TEST_MODE_PACKETS_PER_BLOCK) != 0)
#error "MT_DUMP_PACKETS must be a whole number of blocks (multiple of 128)"
#endif

static SystemContext mt_ctx;
static SystemState mt_last_state;
static uint8_t mt_packet[NI_PACKET_BYTES];
static uint8_t mt_readback[NI_PACKET_BYTES];
static uint32_t mt_format_number;

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
    log_line("STOP: %s\r\n", reason);
    debug_log_write("===== NAND MODES TEST FAILED =====\r\n");

    for (;;) {
    }
}

/* ------------------------------------------------------------------------- */
/* Super-loop (as main.c, without the watchdog)                               */
/* ------------------------------------------------------------------------- */

static void init_system_context(SystemContext* ctx) {
    (void)memset(ctx, 0, sizeof(*ctx));

    ctx->state = STATE_INIT;
    ctx->previous_state = STATE_INIT;
    ctx->alarm_status = 0U;
    ctx->alarm_mask = ALARM_ALL_MASK;
    ctx->masked_alarm = 0U;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

static void log_state_change(void) {
    if (mt_ctx.state != mt_last_state) {
        log_line("STATE %s -> %s alarm=0x%04lX masked=0x%04lX\r\n",
                 state_name(mt_last_state), state_name(mt_ctx.state),
                 (unsigned long)mt_ctx.alarm_status, (unsigned long)mt_ctx.masked_alarm);
        mt_last_state = mt_ctx.state;
    }
}

static void super_loop_step(void) {
    uint32_t now_ms = timebase_millis();

    (void)transport_poll(&mt_ctx, now_ms);
    algorithm_collect_hw_events(&mt_ctx);
    alarm_monitor_poll(&mt_ctx, now_ms);
    algorithm_poll(&mt_ctx);
    algorithm_process_events(&mt_ctx);

    log_state_change();
}

static void log_progress(void) {
    switch (mt_ctx.state) {
    case STATE_ERASE:
        log_line("  erase stage=%u\r\n", (unsigned)mt_ctx.erase.stage);
        break;
    case STATE_TEST:
        log_line("  test stage=%u block=%lu/%lu packet=%lu errors=%lu\r\n",
                 (unsigned)mt_ctx.test.stage,
                 (unsigned long)mt_ctx.test.block_index,
                 (unsigned long)mt_ctx.test.total_blocks,
                 (unsigned long)mt_ctx.test.packet_in_block,
                 (unsigned long)mt_ctx.test.total_errors);
        break;
    case STATE_DUMP:
        log_line("  dump stage=%u bytes=%lu/%lu\r\n",
                 (unsigned)mt_ctx.dump.stage,
                 (unsigned long)mt_ctx.dump.bytes_done,
                 (unsigned long)mt_ctx.dump.size);
        break;
    default:
        break;
    }
}

/* Push a command event and run the super-loop until the FSM leaves mode_state. */
static uint8_t run_mode(const SystemEvent* event, SystemState mode_state, const char* name) {
    uint32_t start_ms;
    uint32_t last_progress_ms;
    uint8_t entered = 0U;

    log_line("%s: command\r\n", name);

    if (!system_event_queue_push_back(event)) {
        halt("event queue full");
    }

    start_ms = timebase_millis();
    last_progress_ms = start_ms;

    for (;;) {
        super_loop_step();

        if (mt_ctx.state == mode_state) {
            entered = 1U;
        } else if ((entered != 0U) || (system_event_queue_get_count() == 0U)) {
            break;
        }

        if (timebase_elapsed(last_progress_ms, MT_PROGRESS_MS)) {
            last_progress_ms = timebase_millis();
            log_progress();
        }

        if (timebase_elapsed(start_ms, MT_MODE_TIMEOUT_MS)) {
            halt("mode timeout");
        }
    }

    log_line("%s: entered=%u, %lu ms, final state %s, alarm=0x%04lX\r\n",
             name, (unsigned)entered, (unsigned long)(timebase_millis() - start_ms),
             state_name(mt_ctx.state), (unsigned long)mt_ctx.alarm_status);

    return ((entered != 0U) && (mt_ctx.state == STATE_DUTY)) ? 1U : 0U;
}

/* ------------------------------------------------------------------------- */
/* NI packets (same generator as ni_fill_test)                                */
/* ------------------------------------------------------------------------- */

static void put_word(uint32_t word_index, uint16_t value) {
    mt_packet[word_index * 2U] = (uint8_t)(value & 0xFFU);
    mt_packet[(word_index * 2U) + 1U] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t mix16(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352DUL;
    value ^= value >> 15;
    value *= 0x846CA68BUL;
    value ^= value >> 16;

    return (uint16_t)(value & 0xFFFFU);
}

static uint32_t build_counters_record(uint32_t word_index, uint32_t rtc_time) {
    uint16_t crc;

    put_word(word_index + 0U, NI_FORMAT_MARK_1);
    put_word(word_index + 1U, NI_FORMAT_MARK_2);
    put_word(word_index + 2U, (uint16_t)(((uint16_t)NI_OBSERVE_MODE << 8) | NI_FORMAT_COUNTERS));
    put_word(word_index + 3U, (uint16_t)(mt_format_number & 0xFFFFU));
    put_word(word_index + 4U, (uint16_t)((mt_format_number >> 16) & 0xFFFFU));
    put_word(word_index + 5U, (uint16_t)(rtc_time & 0xFFFFU));
    put_word(word_index + 6U, (uint16_t)((rtc_time >> 16) & 0xFFFFU));
    put_word(word_index + 7U, (uint16_t)NI_COUNTERS_WORDS);

    crc = crc16_ccitt(&mt_packet[word_index * 2U], 8U * 2U);
    put_word(word_index + 8U, crc);

    put_word(word_index + 9U, mix16(mt_format_number + 0x1111U));
    put_word(word_index + 10U, mix16(mt_format_number + 0x2222U));
    put_word(word_index + 11U, mix16(mt_format_number + 0x3333U));
    put_word(word_index + 12U, mix16(mt_format_number + 0x4444U));
    put_word(word_index + 13U, mix16(mt_format_number + 0x5555U));

    crc = crc16_ccitt(&mt_packet[(word_index + 9U) * 2U], 5U * 2U);
    put_word(word_index + 14U, crc);

    ++mt_format_number;

    return word_index + NI_COUNTERS_WORDS;
}

static uint16_t build_packet(uint32_t packet_index, uint16_t previous_crc) {
    uint32_t word_index = NI_HEADER_WORDS;
    uint32_t core_end = NI_HEADER_WORDS + NI_CORE_WORDS;
    uint16_t crc;

    put_word(0U, NI_MARKER_1);
    put_word(1U, NI_MARKER_2);
    put_word(2U, NI_MARKER_3);
    put_word(3U, (uint16_t)MT_NI_SESSION_ID);
    put_word(4U, (uint16_t)(packet_index & 0xFFFFU));
    put_word(5U, (uint16_t)((packet_index >> 16) & 0xFFFFU));
    put_word(6U, previous_crc);

    while ((word_index + NI_COUNTERS_WORDS) <= core_end) {
        word_index = build_counters_record(word_index, packet_index);
    }

    while (word_index < core_end) {
        put_word(word_index, NI_PAD_WORD);
        ++word_index;
    }

    crc = crc16_ccitt(&mt_packet[NI_HEADER_WORDS * 2U], NI_CORE_WORDS * 2U);
    put_word(NI_PACKET_WORDS - 1U, crc);

    return crc;
}

/* ------------------------------------------------------------------------- */
/* Direct Board_API access between modes (bank on -> work -> bank off)        */
/* ------------------------------------------------------------------------- */

static void bank_open(void) {
    uint8_t is_powered = 0U;

    if (board_nand_power_on((uint8_t)MT_BANK_ID) != BOARD_OK) {
        halt("power on");
    }
    if ((board_nand_is_powered((uint8_t)MT_BANK_ID, &is_powered) != BOARD_OK) || (is_powered == 0U)) {
        halt("bank not powered");
    }
    if (board_nand_connect((uint8_t)MT_BANK_ID) != BOARD_OK) {
        halt("connect");
    }
    if (nand_map_load((uint8_t)MT_BANK_ID) != BOARD_OK) {
        halt("nand_map_load");
    }
}

static void bank_close(void) {
    (void)board_nand_disconnect((uint8_t)MT_BANK_ID);
    (void)board_nand_power_off((uint8_t)MT_BANK_ID);
}

#if (MT_DO_FILL != 0)
static void fill_bank(void) {
    uint32_t packet_index;
    uint16_t previous_crc = 0U;
    uint8_t is_done = 0U;
    uint32_t start_ms = timebase_millis();
    BoardStatus status;

    log_line("FILL: erase + %lu NI packets (session %u)\r\n",
             (unsigned long)MT_DUMP_PACKETS, (unsigned)MT_NI_SESSION_ID);

    bank_open();

    if (board_nand_erase_start((uint8_t)MT_BANK_ID) != BOARD_OK) {
        halt("fill erase start");
    }
    while (is_done == 0U) {
        if (board_nand_erase_is_done((uint8_t)MT_BANK_ID, &is_done) != BOARD_OK) {
            halt("fill erase");
        }
    }

    if (board_nand_open_write((uint8_t)MT_BANK_ID, 0U) != BOARD_OK) {
        halt("fill open_write");
    }

    mt_format_number = 0U;
    for (packet_index = 0U; packet_index < MT_DUMP_PACKETS; ++packet_index) {
        previous_crc = build_packet(packet_index, previous_crc);

        for (;;) {
            status = board_nand_write_packet((uint8_t)MT_BANK_ID, mt_packet);
            if (status != BOARD_ERR_BUSY) {
                break;
            }
            if (board_nand_write_poll((uint8_t)MT_BANK_ID, &is_done) != BOARD_OK) {
                halt("fill write poll");
            }
        }
        if (status != BOARD_OK) {
            halt("fill write");
        }
    }

    is_done = 0U;
    while (is_done == 0U) {
        if (board_nand_write_flush((uint8_t)MT_BANK_ID, &is_done) != BOARD_OK) {
            halt("fill flush");
        }
    }

    bank_close();
    log_line("FILL: done, %lu ms, last_crc=%u\r\n",
             (unsigned long)(timebase_millis() - start_ms), (unsigned)previous_crc);
}
#endif

#if ((MT_DO_ERASE != 0) || (MT_DO_ALARM_RECOVERY != 0))
/* Independent check after ERASE: the first packet of every block must be erased. */
static uint32_t check_bank_erased(void) {
    uint32_t capacity = 0U;
    uint32_t block;
    uint32_t index;
    uint32_t not_erased = 0U;
    BoardStatus status;

    bank_open();

    if ((board_nand_get_capacity_packets((uint8_t)MT_BANK_ID, &capacity) != BOARD_OK) ||
        (board_nand_open_read((uint8_t)MT_BANK_ID, capacity) != BOARD_OK)) {
        halt("erase check open");
    }

    for (block = 0U; block < (capacity / TEST_MODE_PACKETS_PER_BLOCK); ++block) {
        status = board_nand_read_packet((uint8_t)MT_BANK_ID, block * TEST_MODE_PACKETS_PER_BLOCK, mt_readback);
        if (status != BOARD_OK) {
            log_line("  erase check read block %lu status %u\r\n", (unsigned long)block, (unsigned)status);
            ++not_erased;
            continue;
        }

        for (index = 0U; index < NI_PACKET_BYTES; ++index) {
            if (mt_readback[index] != 0xFFU) {
                if (not_erased < 8U) {
                    log_line("  block %lu not erased (byte %lu)\r\n", (unsigned long)block, (unsigned long)index);
                }
                ++not_erased;
                break;
            }
        }
    }

    bank_close();
    log_line("ERASE check: %lu blocks checked, %lu not erased\r\n",
             (unsigned long)(capacity / TEST_MODE_PACKETS_PER_BLOCK), (unsigned long)not_erased);

    return not_erased;
}
#endif

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */

int main(void) {
    SystemEvent event;
    uint32_t failures = 0U;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(MT_START_DELAY_MS);
    log_line("\r\n===== NAND MODES TEST (flight Algorithm), bank %u =====\r\n", (unsigned)MT_BANK_ID);

    init_system_context(&mt_ctx);
    system_event_queue_init();
    mt_last_state = mt_ctx.state;

    (void)system_event_queue_push_back_type(EVENT_BOOT);
    algorithm_process_events(&mt_ctx);
    log_state_change();

    (void)system_event_queue_push_back_type((board_comm_init() == BOARD_OK) ? EVENT_INIT_DONE : EVENT_INIT_FAIL);
    algorithm_process_events(&mt_ctx);
    log_state_change();

    if (mt_ctx.state != STATE_DUTY) {
        halt("FSM did not reach DUTY after boot");
    }

#if (MT_DO_FILL != 0)
    fill_bank();
#endif

#if (MT_DO_DUMP != 0)
    log_line("DUMP: start the PC receiver now: python3 tools/ni_dump_receiver.py --expect %lu\r\n",
             (unsigned long)MT_DUMP_PACKETS);
    log_line("DUMP: starts in %lu ms\r\n", (unsigned long)MT_DUMP_WAIT_MS);
    timebase_delay_ms_blocking(MT_DUMP_WAIT_MS);
    {
        uint8_t present = 0xFFU;
        (void)board_data_link_present(&present);
        log_line("DUMP: PU_USB_VBUS present=%u (FTDI off)\r\n", (unsigned)present);
    }

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.command.dump.bank = MT_NAND_BANK;
    event.command.dump.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.dump.start_address = 0U;
    event.command.dump.requested_packet_count = MT_DUMP_PACKETS;
    event.command.dump.size = MT_DUMP_PACKETS * DUMP_MODE_PACKET_SIZE;
    event.command.dump.dump_all = false;

    if (run_mode(&event, STATE_DUMP, "DUMP") == 0U) {
        ++failures;
    }
    log_line("DUMP: bytes_done=%lu of %lu, last_dumped_packet=%lu, failed=%u\r\n",
             (unsigned long)mt_ctx.dump.bytes_done, (unsigned long)mt_ctx.dump.size,
             (unsigned long)mt_ctx.dump.last_dumped_packet, (unsigned)mt_ctx.dump.operation_failed);
#endif

#if (MT_DO_ERASE != 0)
    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_ERASE;
    event.command.erase.bank = MT_NAND_BANK;
    event.command.erase.power_after_done = POWER_AFTER_DONE_OFF;

    if (run_mode(&event, STATE_ERASE, "ERASE") == 0U) {
        ++failures;
    }
    if (check_bank_erased() != 0U) {
        ++failures;
    }
#endif

#if (MT_DO_TEST != 0)
    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_TEST;
    event.command.test.bank = MT_NAND_BANK;
    event.command.test.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.test.test_mask = 0U;

    if (run_mode(&event, STATE_TEST, "TEST") == 0U) {
        ++failures;
    }

    {
        uint32_t block;
        uint32_t blocks_with_errors = 0U;

        for (block = 0U; block < TEST_MODE_BLOCK_COUNT; ++block) {
            if (mt_ctx.test.nerr[block] != 0U) {
                ++blocks_with_errors;
            }
        }

        log_line("TEST: blocks=%lu total_errors=%lu blocks_with_errors=%lu status=0x%lX valid=%u failed_addr=0x%08lX\r\n",
                 (unsigned long)mt_ctx.test.total_blocks,
                 (unsigned long)mt_ctx.test.total_errors,
                 (unsigned long)blocks_with_errors,
                 (unsigned long)mt_ctx.test.result_status,
                 (unsigned)mt_ctx.test.result_valid,
                 (unsigned long)mt_ctx.test.failed_address);

        if ((mt_ctx.test.total_errors != 0U) || (mt_ctx.test.result_status != TEST_RESULT_STATUS_OK)) {
            ++failures;
        }
    }
#endif

#if (MT_DO_ALARM_RECOVERY != 0)
    {
        uint32_t steps;

        log_line("ALARM: raise ALARM_NAND_PR\r\n");
        alarm_raise(&mt_ctx, ALARM_NAND_PR);
        for (steps = 0U; (steps < 1000U) && (mt_ctx.state != STATE_ALARM); ++steps) {
            super_loop_step();
        }
        if (mt_ctx.state != STATE_ALARM) {
            log_line("ALARM: FSM did not enter ALARM\r\n");
            ++failures;
        } else {
            (void)memset(&event, 0, sizeof(event));
            event.type = EVENT_CMD_RESET_ALARM;
            if (!system_event_queue_push_back(&event)) {
                halt("event queue full");
            }
            for (steps = 0U; (steps < 1000U) && (mt_ctx.state != STATE_DUTY); ++steps) {
                super_loop_step();
            }
            log_line("ALARM: after reset state %s, alarm=0x%04lX\r\n",
                     state_name(mt_ctx.state), (unsigned long)mt_ctx.alarm_status);

            if (mt_ctx.state != STATE_DUTY) {
                ++failures;
            } else {
                (void)memset(&event, 0, sizeof(event));
                event.type = EVENT_CMD_ERASE;
                event.command.erase.bank = MT_NAND_BANK;
                event.command.erase.power_after_done = POWER_AFTER_DONE_OFF;

                if (run_mode(&event, STATE_ERASE, "ERASE after ALARM") == 0U) {
                    ++failures;
                }
                if (check_bank_erased() != 0U) {
                    ++failures;
                }
            }
        }
    }
#endif

    (void)event;
    log_line("failures=%lu\r\n", (unsigned long)failures);
    debug_log_write((failures == 0U) ? "===== NAND MODES TEST PASSED =====\r\n"
                                     : "===== NAND MODES TEST FAILED =====\r\n");

    for (;;) {
        super_loop_step();
    }
}
