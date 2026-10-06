#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "crc16.h"
#include "event_queue.h"
#include "mram_store.h"
#include "ni_format.h"
#include "ni_stream.h"
#include "observe.h"
#include "state.h"
#include "tlm_staging.h"
#include "transport.h"

#define SETTLE_GUARD 20000U
#define MAX_PACKETS  256U
#define MAX_FORMATS  4096U

#define PARAMS_EVENTS_NMAX_1 0x0009U
#define PARAMS_NO_EVENTS     0x3000U
#define TRIGGER_CONFIG       0x0055U

typedef struct {
    uint8_t type;
    uint8_t mode;
    uint16_t length;
    uint32_t number;
} FormatInfo;

static SystemContext ctx;
static uint8_t packet[NI_PACKET_BYTES];
static uint16_t core[MAX_PACKETS * NI_PACKET_CORE_WORDS];
static FormatInfo formats[MAX_FORMATS];

static uint16_t word_at(const uint8_t *data, size_t index) {
    return (uint16_t)((uint16_t)data[index * 2U] | ((uint16_t)data[(index * 2U) + 1U] << 8));
}

static void begin_test(void) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    ctx.previous_state = STATE_DUTY;
    ctx.alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx.nand1.bank = NAND_BANK_1;
    ctx.nand2.bank = NAND_BANK_2;
    ctx.test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static void enqueue_observe_start(uint16_t params, bool ped_power) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_OBSERVE_START;
    event.command.observe_start.bank = NAND_BANK_1;
    event.command.observe_start.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.observe_start.observe_params = params;
    event.command.observe_start.trigger_config = TRIGGER_CONFIG;
    event.command.observe_start.ped_power_enabled = ped_power;
    event.command.observe_start.registration_enabled = true;
    assert(system_event_queue_push_back(&event));
}

static void enqueue_observe_ctrl(uint16_t params, uint16_t trigger_config) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_OBSERVE_CTRL;
    event.command.observe_ctrl.ped_power_enabled = true;
    event.command.observe_ctrl.registration_enabled = true;
    event.command.observe_ctrl.observe_params = params;
    event.command.observe_ctrl.trigger_config = trigger_config;
    assert(system_event_queue_push_back(&event));
}

static void enqueue_duty(void) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUTY;
    event.command.duty.bank = NAND_BANK_1;
    event.command.duty.power_after_done = POWER_AFTER_DONE_OFF;
    assert(system_event_queue_push_back(&event));
}

static void enqueue_mcilwain(int16_t l_value, int16_t b_value) {
    uint8_t kt[NI_KT_MCILWAIN_BYTES];
    SystemEvent event;

    memset(kt, 0, sizeof(kt));
    kt[20] = (uint8_t)((uint16_t)l_value & 0xFFU);
    kt[21] = (uint8_t)((uint16_t)l_value >> 8);
    kt[22] = (uint8_t)((uint16_t)b_value & 0xFFU);
    kt[23] = (uint8_t)((uint16_t)b_value >> 8);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_MCILWAIN;
    event.tlm_slot = tlm_staging_put(kt, (uint16_t)sizeof(kt));
    assert(system_event_queue_push_back(&event));
}

static void push_event(uint16_t index) {
    BoardPedRecord record;

    memset(&record, 0, sizeof(record));
    record.kind = BOARD_PED_RECORD_EVENT;
    record.data[0] = (uint16_t)(0x1000U + index);
    record.data[1] = (uint16_t)(0x2000U + index);
    record.data[2] = (uint16_t)(0x3000U + index);
    record.data[3] = (uint16_t)(0x4000U + index);
    assert(board_stub_push_ped_record(&record));
}

static void push_second(uint32_t rtc_seconds) {
    BoardPedRecord record;

    memset(&record, 0, sizeof(record));
    record.kind = BOARD_PED_RECORD_SECOND;
    record.data[0] = 11U;
    record.data[1] = 22U;
    record.data[2] = 33U;
    record.data[3] = 44U;
    record.data[4] = 55U;
    record.rtc_seconds = rtc_seconds;
    assert(board_stub_push_ped_record(&record));
}

static void step(void) {
    algorithm_poll(&ctx);
    algorithm_process_events(&ctx);
}

static void run_steps(uint32_t count) {
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        step();
    }
}

static void wait_stage(ObserveStage stage) {
    uint32_t guard;

    for (guard = 0U; (guard < SETTLE_GUARD) && (ctx.observe.stage != stage); ++guard) {
        step();
    }

    assert(ctx.observe.stage == stage);
}

static void wait_state(SystemState state) {
    uint32_t guard;

    for (guard = 0U; (guard < SETTLE_GUARD) && (ctx.state != state); ++guard) {
        step();
    }

    assert(ctx.state == state);
}

static void start_session(uint16_t params, bool ped_power, bool events_wait_kt) {
    enqueue_observe_start(params, ped_power);
    step();
    assert(ctx.state == STATE_OBSERVE);
    assert(ctx.observe.events_wait_kt == (NATALIA_OBSERVE_EVENTS_WAIT_KT != 0));
    ctx.observe.events_wait_kt = events_wait_kt;
    wait_stage(OBSERVE_STAGE_ACTIVE);
    assert(board_stub_ped_acquisition_active());
}

/* Reads packets [first, first + count) of bank 1, checks headers, the CRC chain
 * and the core CRC, appends the cores to core[] and returns the number of words. */
static size_t read_cores(uint32_t first, uint32_t count, uint16_t session, uint16_t previous_crc) {
    size_t words = 0U;
    uint32_t index;
    size_t i;

    assert(board_nand_open_read(1U, first + count) == BOARD_OK);

    for (index = first; index < (first + count); ++index) {
        assert(board_nand_read_packet(1U, index, packet) == BOARD_OK);

        assert(word_at(packet, 0U) == NI_PACKET_MARKER_1);
        assert(word_at(packet, 1U) == NI_PACKET_MARKER_2);
        assert(word_at(packet, 2U) == NI_PACKET_MARKER_3);
        assert(word_at(packet, 3U) == session);
        assert(((uint32_t)word_at(packet, 4U) | ((uint32_t)word_at(packet, 5U) << 16)) == index);
        assert(word_at(packet, 6U) == previous_crc);
        assert(crc16_ccitt_software(&packet[NI_PACKET_HEADER_WORDS * 2U], NI_PACKET_CORE_WORDS * 2U) ==
               word_at(packet, NI_PACKET_WORDS - 1U));

        for (i = 0U; i < NI_PACKET_CORE_WORDS; ++i) {
            core[words++] = word_at(packet, NI_PACKET_HEADER_WORDS + i);
        }

        previous_crc = word_at(packet, NI_PACKET_WORDS - 1U);
    }

    return words;
}

/* Splits the core stream into formats; returns the count, *padded = AAAAh tail seen. */
static size_t parse_formats(size_t total, bool *padded) {
    size_t count = 0U;
    size_t i = 0U;

    *padded = false;

    while (i < total) {
        if (core[i] == NI_PACKET_PAD_WORD) {
            for (; i < total; ++i) {
                assert(core[i] == NI_PACKET_PAD_WORD);
            }
            *padded = true;
            break;
        }

        assert(core[i] == NI_FORMAT_MARK);
        assert((core[i + 1U] >> 8) == NI_FORMAT_CLASS_CODE);
        assert(count < MAX_FORMATS);

        formats[count].type = (uint8_t)(core[i + 1U] & 0xFFU);
        formats[count].mode = (uint8_t)(core[i + 2U] & 0xFFU);
        formats[count].number = (uint32_t)core[i + 3U] | ((uint32_t)core[i + 4U] << 16);
        formats[count].length = core[i + 7U];
        assert(formats[count].length > NI_FORMAT_HEADER_WORDS);

        i += formats[count].length;
        ++count;
    }

    return count;
}

static void assert_formats(size_t count, const uint8_t *types, size_t expected) {
    size_t i;

    assert(count == expected);

    for (i = 0U; i < count; ++i) {
        assert(formats[i].type == types[i]);
        assert(formats[i].number == i);
    }
}

static MramStoreServiceData load_service(void) {
    MramStoreServiceData service;

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);

    return service;
}

/* Full session: first second = Telemetry, Nmax = 1 events, a McIlwain KT, KU 6
 * waits for the next second and closes with Counters + Telemetry and AAAAh. */
static void session_writes_formats_and_finishes_on_next_second(void) {
    static const uint8_t expected[] = {
        NI_FORMAT_TELEMETRY,
        NI_FORMAT_EVENTS, NI_FORMAT_EVENTS, NI_FORMAT_EVENTS,
        NI_FORMAT_COUNTERS,
        NI_FORMAT_MCILWAIN,
        NI_FORMAT_EVENTS, NI_FORMAT_EVENTS,
        NI_FORMAT_COUNTERS,
        NI_FORMAT_TELEMETRY
    };
    MramStoreServiceData service;
    uint32_t packets;
    size_t words;
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);
    assert(ctx.observe_session_id == 1U);

    push_event(0U);
    push_second(100U);
    push_event(1U);
    push_event(2U);
    push_event(3U);
    push_second(101U);
    run_steps(4U);

    enqueue_mcilwain(250, -5);
    run_steps(2U);

    enqueue_duty();
    step();
    assert(ctx.state == STATE_OBSERVE);
    assert(ctx.observe.stage == OBSERVE_STAGE_FINISHING);

    push_event(4U);
    push_event(5U);
    run_steps(4U);
    assert(ctx.state == STATE_OBSERVE);

    push_second(102U);
    wait_state(STATE_DUTY);

    assert(ctx.observe.stage == OBSERVE_STAGE_EXIT_CMD);
    assert(!board_stub_ped_acquisition_active());
    assert(ctx.alarm_status == 0U);

    packets = ctx.observe.committed_packet_count;
    assert(packets == 1U);

    service = load_service();
    assert(service.nand1_packet_count == packets);
    assert(service.observe_session_id == 1U);

    words = read_cores(0U, packets, 1U, 0U);
    count = parse_formats(words, &padded);
    assert(padded);
    assert_formats(count, expected, sizeof(expected));
    assert(formats[1].length == ni_format_events_words(1U));
    assert(formats[4].length == NI_FORMAT_COUNTERS_WORDS);
    assert(formats[9].length == NI_FORMAT_TELEMETRY_WORDS);
}

/* Without a second mark after KU 6 the mode closes after OBSERVE_FINISH_TIMEOUT_S
 * RTC ticks: no final Counters / Telemetry, the session is still padded. */
static void finish_times_out_without_second(void) {
    static const uint8_t expected[] = {
        NI_FORMAT_TELEMETRY,
        NI_FORMAT_EVENTS, NI_FORMAT_EVENTS
    };
    uint32_t tick;
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);

    push_second(200U);
    push_event(0U);
    push_event(1U);
    run_steps(3U);

    enqueue_duty();
    run_steps(3U);
    assert(ctx.state == STATE_OBSERVE);

    for (tick = 0U; tick < OBSERVE_FINISH_TIMEOUT_S; ++tick) {
        (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
        step();
    }

    wait_state(STATE_DUTY);

    count = parse_formats(read_cores(0U, ctx.observe.committed_packet_count, 1U, 0U), &padded);
    assert(padded);
    assert_formats(count, expected, sizeof(expected));
}

/* KU 5: hardware bits and PED registers at once, observe parameters at the next
 * second: Counters of the old second in mode 0, Telemetry in mode 1, then no
 * Events formats. */
static void ctrl_applies_params_on_next_second(void) {
    static const uint8_t expected[] = {
        NI_FORMAT_TELEMETRY,
        NI_FORMAT_EVENTS,
        NI_FORMAT_COUNTERS, NI_FORMAT_TELEMETRY,
        NI_FORMAT_COUNTERS,
        NI_FORMAT_COUNTERS, NI_FORMAT_TELEMETRY
    };
    uint8_t address = 0U;
    uint16_t value = 0U;
    size_t writes;
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);
    assert(board_stub_ped_register_write_count() == 0U);

    push_second(300U);
    run_steps(2U);

    enqueue_observe_ctrl(PARAMS_NO_EVENTS, 0x00AAU);
    step();

    writes = board_stub_ped_register_write_count();
    assert(writes == 2U);
    assert(board_stub_ped_register_write_at(0U, &address, &value));
    assert((address == 0x80U) && (value == 0x00AAU));
    assert(board_stub_ped_register_write_at(1U, &address, &value));
    assert((address == 0x81U) && (value == 3U));

    push_event(0U);
    push_second(301U);
    push_event(1U);
    push_second(302U);
    run_steps(4U);

    enqueue_duty();
    step();
    push_second(303U);
    wait_state(STATE_DUTY);

    count = parse_formats(read_cores(0U, ctx.observe.committed_packet_count, 1U, 0U), &padded);
    assert_formats(count, expected, sizeof(expected));
    assert(formats[1].mode == 0U);
    assert(formats[2].mode == 0U);
    assert(formats[3].mode == 1U);
    assert(formats[4].mode == 1U);
}

/* A record batch is limited per pass: the rest stays in the ring for later passes. */
static void records_per_pass_are_limited(void) {
    uint16_t i;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);

    push_second(400U);
    for (i = 0U; i < 200U; ++i) {
        push_event(i);
    }

    algorithm_poll(&ctx);
    assert(ctx.observe.stats.seconds_taken == 1U);
    assert(ctx.observe.stats.events_taken == (OBSERVE_RECORDS_PER_POLL - 1U));

    run_steps(4U);
    assert(ctx.observe.stats.events_taken == 200U);
}

/* A second session continues the bank: next packet number, CRC chain across
 * sessions, new session id. */
static void second_session_continues_the_bank(void) {
    uint32_t first_packets;
    uint16_t last_crc;
    size_t words;
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);
    push_second(500U);
    push_event(0U);
    run_steps(2U);
    enqueue_duty();
    step();
    push_second(501U);
    wait_state(STATE_DUTY);

    first_packets = ctx.observe.committed_packet_count;
    assert(first_packets == 1U);
    assert(board_nand_read_packet(1U, first_packets - 1U, packet) == BOARD_OK);
    last_crc = word_at(packet, NI_PACKET_WORDS - 1U);

    start_session(PARAMS_EVENTS_NMAX_1, false, false);
    assert(ctx.observe_session_id == 2U);
    push_second(600U);
    run_steps(2U);
    enqueue_duty();
    step();
    push_second(601U);
    wait_state(STATE_DUTY);

    assert(ctx.observe.committed_packet_count == (first_packets + 1U));
    words = read_cores(first_packets, 1U, 2U, last_crc);
    count = parse_formats(words, &padded);
    assert(padded);
    assert(count == 3U);
    assert(formats[0].type == NI_FORMAT_TELEMETRY);
    assert(formats[0].number == 0U);
    assert(load_service().nand1_packet_count == (first_packets + 1U));
}

/* KU 6 before the writer has recovered: no session, straight to DUTY. */
static void duty_during_enter_finishes_at_once(void) {
    begin_test();

    enqueue_observe_start(PARAMS_EVENTS_NMAX_1, false);
    enqueue_duty();
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.observe.stage == OBSERVE_STAGE_EXIT_CMD);
    assert(ctx.observe_session_id == 0U);
    assert(!board_stub_ped_acquisition_active());
}

/* A NAND write error stops the session: ALARM_NAND_PR, acquisition off, ALARM. */
static void nand_write_failure_enters_alarm(void) {
    uint16_t i;

    begin_test();
    board_stub_set_nand_write_fail_at(1U, 0U);
    start_session(PARAMS_EVENTS_NMAX_1, false, false);

    push_second(700U);
    for (i = 0U; i < 100U; ++i) {
        push_event(i);
    }

    wait_state(STATE_ALARM);

    assert((ctx.alarm_status & ALARM_NAND_PR) != 0U);
    assert(ctx.observe.operation_failed);
    assert(!board_stub_ped_acquisition_active());
}

/* Filling the bank ends the mode in DUTY with the bank marked full and the
 * packet counter in MRAM at the bank capacity. */
static void full_bank_ends_in_duty(void) {
    uint32_t capacity = 0U;
    uint32_t guard;
    uint16_t index = 0U;
    uint32_t second = 800U;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, false);
    assert(board_nand_get_capacity_packets(1U, &capacity) == BOARD_OK);

    push_second(second);

    for (guard = 0U; (guard < SETTLE_GUARD) && (ctx.state == STATE_OBSERVE); ++guard) {
        BoardPedRecord record;
        uint32_t i;

        for (i = 0U; i < 64U; ++i) {
            memset(&record, 0, sizeof(record));
            record.kind = BOARD_PED_RECORD_EVENT;
            record.data[2] = index++;
            if (!board_stub_push_ped_record(&record)) {
                break;
            }
        }
        if ((guard % 50U) == 0U) {
            ++second;
            (void)board_stub_push_ped_record(&(BoardPedRecord){.kind = BOARD_PED_RECORD_SECOND,
                                                               .rtc_seconds = second});
        }

        step();
    }

    assert(ctx.state == STATE_DUTY);
    assert(ctx.observe.stage == OBSERVE_STAGE_EXIT_FULL);
    assert(ctx.nand1.is_full);
    assert(ctx.observe.committed_packet_count == capacity);
    assert(load_service().nand1_packet_count == capacity);
    assert(!board_stub_ped_acquisition_active());
}

/* Default rule: no Events format until the first KT of the session. Events before
 * the KT only go to the spectrum; after the KT they are written. */
static void events_wait_for_first_kt(void) {
    static const uint8_t expected[] = {
        NI_FORMAT_TELEMETRY,
        NI_FORMAT_MCILWAIN,
        NI_FORMAT_EVENTS, NI_FORMAT_EVENTS,
        NI_FORMAT_COUNTERS,
        NI_FORMAT_COUNTERS, NI_FORMAT_TELEMETRY
    };
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, true);

    push_second(900U);
    push_event(0U);
    push_event(1U);
    run_steps(3U);

    enqueue_mcilwain(250, -5);
    run_steps(2U);

    push_event(2U);
    push_event(3U);
    push_second(901U);
    run_steps(3U);

    enqueue_duty();
    step();
    push_second(902U);
    wait_state(STATE_DUTY);

    count = parse_formats(read_cores(0U, ctx.observe.committed_packet_count, 1U, 0U), &padded);
    assert(padded);
    assert_formats(count, expected, sizeof(expected));
    assert(core[(NI_FORMAT_TELEMETRY_WORDS + NI_FORMAT_MCILWAIN_WORDS) + NI_FORMAT_HEADER_WORDS] ==
           0x1002U);
}

/* A KT received before the first second (it only updates the conditions, T6)
 * also unlocks the Events format from the first full second. */
static void kt_before_first_second_unlocks_events(void) {
    static const uint8_t expected[] = {
        NI_FORMAT_TELEMETRY,
        NI_FORMAT_EVENTS,
        NI_FORMAT_COUNTERS,
        NI_FORMAT_COUNTERS, NI_FORMAT_TELEMETRY
    };
    size_t count;
    bool padded;

    begin_test();
    start_session(PARAMS_EVENTS_NMAX_1, false, true);

    enqueue_mcilwain(250, -5);
    run_steps(2U);

    push_second(950U);
    push_event(0U);
    push_second(951U);
    run_steps(3U);

    enqueue_duty();
    step();
    push_second(952U);
    wait_state(STATE_DUTY);

    count = parse_formats(read_cores(0U, ctx.observe.committed_packet_count, 1U, 0U), &padded);
    assert(padded);
    assert_formats(count, expected, sizeof(expected));
}

int main(void) {
    session_writes_formats_and_finishes_on_next_second();
    finish_times_out_without_second();
    ctrl_applies_params_on_next_second();
    records_per_pass_are_limited();
    second_session_continues_the_bank();
    duty_during_enter_finishes_at_once();
    nand_write_failure_enters_alarm();
    full_bank_ends_in_duty();
    events_wait_for_first_kt();
    kt_before_first_second_unlocks_events();

    return 0;
}
