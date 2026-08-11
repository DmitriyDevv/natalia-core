#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

#define KU_STATUS_REQ_MSG_ID (0x0F01U)
#define KU_TEST_MSG_ID       (0x0F09U)
#define TS_STATUS_MSG_ID     (0x0D00U)
#define TS_ACK_MSG_ID        (0x0D01U)
#define ACK_ERR_MODE         (0x07U)
#define SHORT_PAYLOAD_SIZE   (6U)
#define FILL_BYTE            (0xAAU)
#define ADDR_NA              (0x1EU)
#define ADDR_BVS             (0x05U)

static void begin_test(SystemContext *ctx, SystemState state) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = state;
    ctx->previous_state = state;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static void save_erase_baseline(uint16_t nand1_count) {
    MramStoreServiceData service;
    memset(&service, 0, sizeof(service));
    service.nand1_erase_count = nand1_count;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static uint16_t load_nand1_erase_count(void) {
    MramStoreServiceData service;
    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    return service.nand1_erase_count;
}

static void enqueue_erase(NandBank bank, PowerAfterDone power_after_done) {
    SystemEvent event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_ERASE;
    event.command.erase.bank = bank;
    event.command.erase.power_after_done = power_after_done;
    assert(system_event_queue_push_back(&event));
}

static void run_until_duty(SystemContext *ctx) {
    uint32_t guard;
    for (guard = 0U; (guard < 100U) && (ctx->state != STATE_DUTY); ++guard) {
        algorithm_poll(ctx);
        algorithm_process_events(ctx);
    }
}

/* Clean erase from DUTY: runs to completion, clears the bank-full flag, powers
 * the bank off (power_after_done = OFF), increments the per-bank erase counter,
 * and returns to DUTY. */
static void clean_erase_increments_counter_and_returns_to_duty(void) {
    SystemContext ctx;
    uint8_t powered = 1U;

    begin_test(&ctx, STATE_DUTY);
    save_erase_baseline(5U);

    ctx.nand1.is_full = true;

    enqueue_erase(NAND_BANK_1, POWER_AFTER_DONE_OFF);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ERASE);

    run_until_duty(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(!ctx.nand1.is_full);
    assert((board_nand_is_powered(1U, &powered) == BOARD_OK) && (powered == 0U));
    assert(load_nand1_erase_count() == 6U); /* incremented on normal completion */
}

/* power_after_done = KEEP leaves the bank powered after a completed erase. */
static void clean_erase_keeps_power_when_requested(void) {
    SystemContext ctx;
    uint8_t powered = 0U;

    begin_test(&ctx, STATE_DUTY);
    save_erase_baseline(0U);

    enqueue_erase(NAND_BANK_1, POWER_AFTER_DONE_KEEP);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ERASE);

    run_until_duty(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert((board_nand_is_powered(1U, &powered) == BOARD_OK) && (powered == 1U));
    assert(load_nand1_erase_count() == 1U);
}

/* Early finish by CMD_DUTY (before ERASE_DONE) returns to DUTY without counting
 * the interrupted erase. */
static void early_finish_by_cmd_duty_does_not_count(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUTY);
    save_erase_baseline(5U);

    enqueue_erase(NAND_BANK_1, POWER_AFTER_DONE_OFF);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ERASE);

    /* Interrupt before polling the erase to completion. */
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUTY;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(load_nand1_erase_count() == 5U); /* unchanged: erase did not complete */
}

/* A masked alarm during ERASE forces ALARM with PED and both NAND banks off,
 * and does not count the erase. */
static void masked_alarm_in_erase_enters_alarm(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t powered1 = 1U;
    uint8_t powered2 = 1U;

    begin_test(&ctx, STATE_ERASE);
    save_erase_baseline(5U);

    ctx.erase.bank = NAND_BANK_1;
    ctx.erase.stage = ERASE_STAGE_WAIT;
    assert(board_nand_power_on(1U) == BOARD_OK);
    assert(board_nand_power_on(2U) == BOARD_OK);
    ctx.ped.is_powered = true;
    ctx.alarm_status = 0x0001UL;
    ctx.masked_alarm = 0x0001UL;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_MASKED_ALARM_SET;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert(!ctx.ped.is_powered);
    assert((board_nand_is_powered(1U, &powered1) == BOARD_OK) && (powered1 == 0U));
    assert((board_nand_is_powered(2U, &powered2) == BOARD_OK) && (powered2 == 0U));
    assert(load_nand1_erase_count() == 5U);
}

/* CMD_STATUS_REQ is answered with a status TS and keeps the mode. */
static void status_req_allowed_in_erase(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_ERASE);

    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    algorithm_process_events(&ctx);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, &length));
    assert(message_id == TS_STATUS_MSG_ID);
    assert(ctx.state == STATE_ERASE);
}

/* A forbidden KU (well-formed CMD_TEST) is rejected with ERR_MODE and keeps the
 * mode. */
static void forbidden_command_rejected_with_err_mode(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_ERASE);

    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    payload[0] = 0x01U;
    board_comm_stub_inject_rx(KU_TEST_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    algorithm_process_events(&ctx);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, reply, sizeof(reply),
                                   &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(reply[0] == (uint8_t)(KU_TEST_MSG_ID & 0xFFU));
    assert(reply[1] == (uint8_t)(KU_TEST_MSG_ID >> 8U));
    assert(reply[2] == ACK_ERR_MODE);
    assert(ctx.state == STATE_ERASE);
}

/* KT (telemetry) messages are ignored in ERASE with no quittance. */
static void telemetry_ignored_in_erase(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_ERASE);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_ORBIT;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_ERASE);
    assert(board_comm_stub_tx_count() == 0U);
}

int main(void) {
    clean_erase_increments_counter_and_returns_to_duty();
    clean_erase_keeps_power_when_requested();
    early_finish_by_cmd_duty_does_not_count();
    masked_alarm_in_erase_enters_alarm();
    status_req_allowed_in_erase();
    forbidden_command_rejected_with_err_mode();
    telemetry_ignored_in_erase();

    return 0;
}
