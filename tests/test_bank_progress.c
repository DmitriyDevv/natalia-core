#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "algorithm.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

/* Bank progress in MRAM (packet counter, last CRC, full flag) and the OBSERVE
 * session number. ERASE and TEST destroy the bank contents, so at their start the
 * bank is marked "needs erase" (counter 0, full = 1); only a completed ERASE or
 * TEST clears the mark. An interrupted one leaves the bank unusable for OBSERVE
 * until the next ERASE. */

#define TS_TELEMETRY_MSG_ID (0x0D02U)
#define TELEMETRY_SIZE      (109U)
#define GUARD               5000U

static void begin_test(SystemContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_DUTY;
    ctx->alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static void store_progress(uint32_t nand1_packets, uint16_t session_id) {
    MramStoreConfig config;
    MramStoreServiceData service;

    memset(&config, 0, sizeof(config));
    config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    assert(mram_store_save_config(&config) == BOARD_OK);

    memset(&service, 0, sizeof(service));
    service.nand1_packet_count = nand1_packets;
    service.nand1_last_packet_crc = 0x1234U;
    service.observe_session_id = session_id;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static MramStoreServiceData load_service(void) {
    MramStoreServiceData service;

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    return service;
}

static void push(EventType type, NandBank bank) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    if (type == EVENT_CMD_ERASE) {
        event.command.erase.bank = bank;
        event.command.erase.power_after_done = POWER_AFTER_DONE_OFF;
    } else if (type == EVENT_CMD_TEST) {
        event.command.test.bank = bank;
        event.command.test.power_after_done = POWER_AFTER_DONE_OFF;
    } else if (type == EVENT_CMD_OBSERVE_START) {
        event.command.observe_start.bank = bank;
        event.command.observe_start.power_after_done = POWER_AFTER_DONE_OFF;
        event.command.observe_start.observe_params = 0x0011U;
        event.command.observe_start.registration_enabled = true;
    } else if (type == EVENT_CMD_DUTY) {
        event.command.duty.bank = bank;
        event.command.duty.power_after_done = POWER_AFTER_DONE_OFF;
    }
    assert(system_event_queue_push_back(&event));
}

static void run_until_duty(SystemContext *ctx) {
    uint32_t guard;

    algorithm_process_events(ctx);
    assert(ctx->state != STATE_DUTY);
    for (guard = 0U; (guard < GUARD) && (ctx->state != STATE_DUTY); ++guard) {
        algorithm_poll(ctx);
        algorithm_process_events(ctx);
    }
    assert(ctx->state == STATE_DUTY);
}

static void assert_bank1_needs_erase(const SystemContext *ctx) {
    MramStoreServiceData service = load_service();

    assert(ctx->nand1.is_full);
    assert(service.nand1_full == 1U);
    assert(service.nand1_packet_count == 0U);
    assert(service.nand1_last_packet_crc == 0U);
}

static void assert_observe_refused(SystemContext *ctx) {
    push(EVENT_CMD_OBSERVE_START, NAND_BANK_1);
    algorithm_process_events(ctx);
    assert(ctx->state == STATE_DUTY);
}

static void interrupted_erase_marks_bank(void) {
    SystemContext ctx;
    MramStoreServiceData service;

    begin_test(&ctx);
    store_progress(50U, 3U);

    push(EVENT_CMD_ERASE, NAND_BANK_1);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ERASE);
    push(EVENT_CMD_DUTY, NAND_BANK_1);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUTY);

    assert_bank1_needs_erase(&ctx);
    assert_observe_refused(&ctx);

    push(EVENT_CMD_ERASE, NAND_BANK_1);
    run_until_duty(&ctx);

    service = load_service();
    assert(!ctx.nand1.is_full);
    assert(service.nand1_full == 0U);
    assert(service.nand1_packet_count == 0U);
    assert(service.observe_session_id == 3U);
}

static void interrupted_test_marks_bank_completed_test_clears(void) {
    SystemContext ctx;
    MramStoreServiceData service;

    begin_test(&ctx);
    store_progress(50U, 0U);

    push(EVENT_CMD_TEST, NAND_BANK_1);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_TEST);
    algorithm_poll(&ctx);
    push(EVENT_CMD_DUTY, NAND_BANK_1);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUTY);

    assert_bank1_needs_erase(&ctx);
    assert_observe_refused(&ctx);

    push(EVENT_CMD_TEST, NAND_BANK_1);
    run_until_duty(&ctx);

    service = load_service();
    assert(ctx.test.result_valid);
    assert(!ctx.nand1.is_full);
    assert(service.nand1_full == 0U);
    assert(service.nand1_packet_count == 0U);
}

static void failed_test_keeps_bank_marked(void) {
    SystemContext ctx;

    begin_test(&ctx);
    store_progress(50U, 0U);
    board_stub_set_nand_write_fail_at(1U, 0U);

    push(EVENT_CMD_TEST, NAND_BANK_1);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_TEST);
    {
        uint32_t guard;

        for (guard = 0U; (guard < GUARD) && (ctx.state == STATE_TEST); ++guard) {
            algorithm_poll(&ctx);
            algorithm_process_events(&ctx);
        }
    }

    assert(ctx.state == STATE_ALARM);
    assert_bank1_needs_erase(&ctx);
}

static void session_id_survives_reboot(void) {
    SystemContext ctx;

    begin_test(&ctx);
    store_progress(0U, 7U);
    ctx.state = STATE_INIT;

    assert(action_load_mram(&ctx) == ACTION_OK);
    assert(ctx.observe_session_id == 7U);
}

static uint32_t telemetry_nand1_count(SystemContext *ctx) {
    uint8_t got[TELEMETRY_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    assert(action_send_telem(ctx) == ACTION_OK);
    assert(transport_poll(ctx, 0U) == BOARD_OK);
    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got), &length));
    assert(message_id == TS_TELEMETRY_MSG_ID);
    assert(length == TELEMETRY_SIZE);

    return (uint32_t)got[84] | ((uint32_t)got[85] << 8) | ((uint32_t)got[86] << 16);
}

static void telemetry_reports_live_count_in_observe(void) {
    SystemContext ctx;

    begin_test(&ctx);
    store_progress(128U, 1U);

    assert(telemetry_nand1_count(&ctx) == 128U);

    ctx.state = STATE_OBSERVE;
    ctx.observe.bank = NAND_BANK_1;
    ctx.observe.committed_packet_count = 201U;
    assert(telemetry_nand1_count(&ctx) == 201U);
}

int main(void) {
    interrupted_erase_marks_bank();
    interrupted_test_marks_bank_completed_test_clears();
    failed_test_keeps_bank_marked();
    session_id_survives_reboot();
    telemetry_reports_live_count_in_observe();

    return 0;
}
