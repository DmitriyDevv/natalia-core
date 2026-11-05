#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "algorithm.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

/* T-03 DUTY transitions and answers, the rest of T-04 (bank full) and T-09
 * (instrument time): KU 2 and KU 0401h set the clock, bit 1 of the CAN control
 * word drops KU 0401h silently, and the initial instrument time from KU 8 is the
 * value the clock starts from at the next boot. */

#define TS_STATUS_MSG_ID      (0x0D00U)
#define TS_ACK_MSG_ID         (0x0D01U)
#define TS_TELEMETRY_MSG_ID   (0x0D02U)
#define TS_TEST_RESULT_MSG_ID (0x0D03U)
#define TS_VERSION_MSG_ID     (0xFFE1U)
#define KU_SET_TIME_ID        (0x0F02U)
#define KU_OBSERVE_START_ID   (0x0F03U)
#define KU_DUTY_ID            (0x0F05U)
#define KU_DUMP_ID            (0x0F06U)
#define KU_SET_CFG_ID         (0x0F07U)
#define KU_ERASE_ID           (0x0F08U)
#define KU_TEST_ID            (0x0F09U)
#define KU_SPUTNIKS_TIME_ID   (0x0401U)
#define ADDR_NA               (0x1EU)
#define ADDR_BVS              (0x05U)
#define ACK_OK                (0x00U)
#define ACK_ERR_CONTENT       (0x05U)
#define SET_CFG_SIZE          (68U)

static void begin_duty(SystemContext *ctx) {
    MramStoreConfig config;
    MramStoreServiceData service;

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_INIT;
    ctx->alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
    ctx->thresholds_loaded = true;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&config, 0, sizeof(config));
    config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    assert(mram_store_save_config(&config) == BOARD_OK);
    memset(&service, 0, sizeof(service));
    service.nand1_packet_count = 3U;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static SystemEvent command(EventType type, uint16_t msg_id) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = msg_id;
    return event;
}

static void flush_tx(SystemContext *ctx) {
    uint32_t i;

    for (i = 0U; i < 24U; ++i) {
        (void)transport_poll(ctx, 0U);
    }
}

static bool last_of(uint16_t wanted, uint8_t *payload, uint16_t capacity, uint16_t *length) {
    uint32_t total = board_comm_stub_tx_count();
    uint32_t i;
    uint16_t message_id;

    for (i = total; i > 0U; --i) {
        if (!board_comm_stub_tx_at(i - 1U, &message_id, NULL, NULL, 0U, NULL)) {
            return false;
        }
        if (message_id == wanted) {
            assert(board_comm_stub_tx_at(i - 1U, &message_id, NULL, payload, capacity, length));
            return true;
        }
    }
    return false;
}

static void assert_last_ack(uint16_t command_id, uint8_t status) {
    uint8_t payload[6];

    assert(last_of(TS_ACK_MSG_ID, payload, sizeof(payload), NULL));
    assert(payload[0] == (uint8_t)(command_id & 0xFFU));
    assert(payload[1] == (uint8_t)(command_id >> 8U));
    assert(payload[2] == status);
}

static uint8_t last_status_byte(uint32_t index) {
    uint8_t payload[6];

    assert(last_of(TS_STATUS_MSG_ID, payload, sizeof(payload), NULL));
    return payload[index];
}

static void run(SystemContext *ctx, const SystemEvent *event) {
    (void)handle_event(ctx, event);
    flush_tx(ctx);
}

static void erase_from_duty(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_ERASE, KU_ERASE_ID);

    begin_duty(&ctx);
    event.command.erase.bank = NAND_BANK_2;
    run(&ctx, &event);

    assert(ctx.state == STATE_ERASE);
    assert(ctx.erase.bank == NAND_BANK_2);
    assert_last_ack(KU_ERASE_ID, ACK_OK);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 3U);
}

static void test_from_duty(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_TEST, KU_TEST_ID);

    begin_duty(&ctx);
    event.command.test.bank = NAND_BANK_1;
    run(&ctx, &event);

    assert(ctx.state == STATE_TEST);
    assert(ctx.test.bank == NAND_BANK_1);
    assert_last_ack(KU_TEST_ID, ACK_OK);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 2U);
}

static void dump_from_duty(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_DUMP, KU_DUMP_ID);
    uint8_t payload[6];

    begin_duty(&ctx);
    event.command.dump.bank = NAND_BANK_1;
    event.command.dump.size = 2U * DUMP_MODE_PACKET_SIZE;
    event.command.dump.requested_packet_count = 2U;
    run(&ctx, &event);

    assert(ctx.state == STATE_DUMP);
    assert(ctx.dump.bank == NAND_BANK_1);
    assert(last_of(TS_ACK_MSG_ID, payload, sizeof(payload), NULL));
    assert(payload[2] == ACK_OK);
    assert(payload[3] == 2U);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 5U);
}

static SystemEvent observe_start(NandBank bank) {
    SystemEvent event = command(EVENT_CMD_OBSERVE_START, KU_OBSERVE_START_ID);

    event.command.observe_start.bank = bank;
    event.command.observe_start.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.observe_start.observe_params = 0x0011U;
    event.command.observe_start.registration_enabled = true;
    event.command.observe_start.ped_power_enabled = true;
    return event;
}

static void observe_from_duty(void) {
    SystemContext ctx;
    SystemEvent event = observe_start(NAND_BANK_1);

    begin_duty(&ctx);
    run(&ctx, &event);

    assert(ctx.state == STATE_OBSERVE);
    assert_last_ack(KU_OBSERVE_START_ID, ACK_OK);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 4U);
}

static void observe_on_full_bank_is_refused(void) {
    SystemContext ctx;
    SystemEvent event = observe_start(NAND_BANK_2);

    begin_duty(&ctx);
    ctx.nand2.is_full = true;
    run(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert_last_ack(KU_OBSERVE_START_ID, ACK_ERR_CONTENT);
}

static void requests_are_answered(void) {
    SystemContext ctx;
    SystemEvent status = command(EVENT_CMD_STATUS_REQ, 0x0F01U);
    SystemEvent telem = command(EVENT_CMD_TELEM_REQ, 0x0F00U);
    SystemEvent version = command(EVENT_CMD_VERSION_REQ, 0xFFE0U);
    SystemEvent result = command(EVENT_CMD_TEST_RESULT, 0x0F0AU);
    uint16_t length = 0U;

    begin_duty(&ctx);
    result.command.test_result.bank = NAND_BANK_1;
    result.command.test_result.mram_copy = 1U;

    run(&ctx, &status);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 1U);

    run(&ctx, &telem);
    assert(last_of(TS_TELEMETRY_MSG_ID, NULL, 0U, &length));
    assert(length == 109U);

    run(&ctx, &version);
    assert(last_of(TS_VERSION_MSG_ID, NULL, 0U, &length));
    assert(length == 3U);

    run(&ctx, &result);
    assert(last_of(TS_TEST_RESULT_MSG_ID, NULL, 0U, &length));
    assert(length == 6146U);
    assert(ctx.state == STATE_DUTY);
}

static void duty_in_duty_is_acknowledged(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_DUTY, KU_DUTY_ID);

    begin_duty(&ctx);
    run(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert_last_ack(KU_DUTY_ID, ACK_OK);
}

static void masked_alarm_enters_alarm_with_safe_config(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_MASKED_ALARM_SET, 0U);

    begin_duty(&ctx);
    ctx.ped.is_powered = true;
    ctx.nand1.is_powered = true;
    ctx.alarm_status = ALARM_PU_TEMP;
    ctx.masked_alarm = ALARM_PU_TEMP;
    run(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert(!ctx.ped.is_powered);
    assert(ctx.ped.inhibit_enabled);
    assert(!ctx.nand1.is_powered);
    assert(((last_status_byte(0U) >> 3U) & 0x07U) == 6U);
}

/* T-04: bank full in OBSERVE. */

static void begin_observe(SystemContext *ctx) {
    SystemEvent event = observe_start(NAND_BANK_1);

    begin_duty(ctx);
    run(ctx, &event);
    assert(ctx->state == STATE_OBSERVE);
}

static void bank_full_sets_status_byte_and_survives_reboot(void) {
    SystemContext ctx;
    SystemContext rebooted;
    SystemEvent event = command(EVENT_NAND_FULL, 0U);

    begin_observe(&ctx);
    run(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert((last_status_byte(1U) & 0x01U) != 0U);

    memset(&rebooted, 0, sizeof(rebooted));
    assert(action_load_mram(&rebooted) == ACTION_OK);
    assert(rebooted.nand1.is_full);
    assert(!rebooted.nand2.is_full);
}

static void bank_full_with_mram_failure_enters_alarm(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_NAND_FULL, 0U);

    begin_observe(&ctx);
    board_stub_set_mram_write_fail(true);
    run(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
}

/* T-10: KT are ignored outside OBSERVE; PED faults in OBSERVE lead to ALARM. */

static void kt_ignored_in_duty(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_TLM_MCILWAIN, 0x0E00U);

    begin_duty(&ctx);
    run(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert(!ctx.observe.kt[OBSERVE_KT_MCILWAIN].pending);
    assert(board_comm_stub_tx_count() == 0U);
}

static void ped_fault_in_observe_enters_alarm(void) {
    SystemContext ctx;

    begin_observe(&ctx);
    board_stub_set_ped_faults(BOARD_PED_FAULT_STATUS);
    algorithm_collect_hw_events(&ctx);
    algorithm_process_events(&ctx);

    assert((ctx.alarm_status & ALARM_PED_ST) != 0U);
    assert(ctx.state == STATE_ALARM);
    assert(!board_stub_ped_acquisition_active());
}

/* T-09: instrument time. */

static void inject(uint16_t message_id, const uint8_t *data, uint16_t length) {
    board_comm_stub_inject_rx(message_id, ADDR_BVS, ADDR_NA, data, length);
}

static void pump(SystemContext *ctx) {
    (void)transport_poll(ctx, 0U);
    algorithm_process_events(ctx);
    flush_tx(ctx);
}

static void set_time_applies_to_clock(void) {
    SystemContext ctx;
    InstrumentTime now;
    uint8_t payload[6] = {0x2CU, 0x01U, 0x78U, 0x56U, 0x34U, 0x12U};

    begin_duty(&ctx);
    inject(KU_SET_TIME_ID, payload, sizeof(payload));
    pump(&ctx);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.milliseconds == 300U);
    assert(now.seconds == 0x12345678UL);
    assert_last_ack(KU_SET_TIME_ID, ACK_OK);
}

static void set_time_rejects_milliseconds_over_999(void) {
    SystemContext ctx;
    InstrumentTime now;
    uint8_t payload[6] = {0xE8U, 0x03U, 0x01U, 0x00U, 0x00U, 0x00U};

    begin_duty(&ctx);
    inject(KU_SET_TIME_ID, payload, sizeof(payload));
    pump(&ctx);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.seconds == 0U);
    assert_last_ack(KU_SET_TIME_ID, ACK_ERR_CONTENT);
}

static void sputniks_time_applies_and_acks_with_its_id(void) {
    SystemContext ctx;
    InstrumentTime now;
    uint8_t payload[5] = {0x10U, 0x00U, 0x00U, 0x00U, 0x00U};

    begin_duty(&ctx);
    inject(KU_SPUTNIKS_TIME_ID, payload, sizeof(payload));
    pump(&ctx);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.seconds == 16U);
    assert_last_ack(KU_SPUTNIKS_TIME_ID, ACK_OK);
}

static void sputniks_time_dropped_when_ignored(void) {
    SystemContext ctx;
    InstrumentTime now;
    uint8_t payload[5] = {0x10U, 0x00U, 0x00U, 0x00U, 0x00U};

    begin_duty(&ctx);
    ctx.can_control = 0x0002U;
    inject(KU_SPUTNIKS_TIME_ID, payload, sizeof(payload));
    pump(&ctx);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.seconds == 0U);
    assert(board_comm_stub_tx_count() == 0U);
}

static void build_set_cfg(uint8_t *data, uint16_t init_ms, uint32_t init_s) {
    memset(data, 0, SET_CFG_SIZE);
    data[42] = (uint8_t)(init_ms & 0xFFU);
    data[43] = (uint8_t)(init_ms >> 8U);
    data[44] = (uint8_t)(init_s & 0xFFU);
    data[45] = (uint8_t)((init_s >> 8U) & 0xFFU);
    data[46] = (uint8_t)((init_s >> 16U) & 0xFFU);
    data[47] = (uint8_t)(init_s >> 24U);
    data[64] = 0xFFU;
    data[65] = 0xFFU;
}

static void initial_time_is_applied_at_boot(void) {
    SystemContext ctx;
    SystemEvent boot = command(EVENT_BOOT, 0U);
    InstrumentTime now;
    uint8_t data[SET_CFG_SIZE];

    begin_duty(&ctx);
    build_set_cfg(data, 250U, 1000000UL);
    inject(KU_SET_CFG_ID, data, SET_CFG_SIZE);
    pump(&ctx);
    assert_last_ack(KU_SET_CFG_ID, ACK_OK);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.seconds == 0U);

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_INIT;
    (void)handle_event(&ctx, &boot);

    assert(board_rtc_get_time(&now) == BOARD_OK);
    assert(now.seconds == 1000000UL);
    assert(now.milliseconds == 250U);
}

static void set_cfg_rejects_initial_milliseconds_over_999(void) {
    SystemContext ctx;
    uint8_t data[SET_CFG_SIZE];

    begin_duty(&ctx);
    build_set_cfg(data, 1000U, 5U);
    inject(KU_SET_CFG_ID, data, SET_CFG_SIZE);
    pump(&ctx);

    assert_last_ack(KU_SET_CFG_ID, ACK_ERR_CONTENT);
}

int main(void) {
    erase_from_duty();
    test_from_duty();
    dump_from_duty();
    observe_from_duty();
    observe_on_full_bank_is_refused();
    requests_are_answered();
    duty_in_duty_is_acknowledged();
    masked_alarm_enters_alarm_with_safe_config();
    bank_full_sets_status_byte_and_survives_reboot();
    bank_full_with_mram_failure_enters_alarm();
    kt_ignored_in_duty();
    ped_fault_in_observe_enters_alarm();
    set_time_applies_to_clock();
    set_time_rejects_milliseconds_over_999();
    sputniks_time_applies_and_acks_with_its_id();
    sputniks_time_dropped_when_ignored();
    initial_time_is_applied_at_boot();
    set_cfg_rejects_initial_milliseconds_over_999();

    return 0;
}
