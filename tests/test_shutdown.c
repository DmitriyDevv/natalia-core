#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

#define KU_STATUS_REQ_MSG_ID (0x0001U)
#define KU_ERASE_MSG_ID      (0x0008U)
#define TS_STATUS_MSG_ID     (0x0200U)
#define TS_ACK_MSG_ID        (0x0201U)
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

/* CMD_SHUTDOWN from DUTY: powers PED and both NAND banks off, saves service
 * data (overwriting the runtime-derived fields while preserving the persisted
 * counters), and transitions to SHUTDOWN. */
static void shutdown_from_duty_powers_down_and_saves(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreServiceData service;
    uint8_t powered1 = 1U;
    uint8_t powered2 = 1U;

    begin_test(&ctx, STATE_DUTY);

    /* Baseline persisted counter that shutdown must preserve. */
    memset(&service, 0, sizeof(service));
    service.nand1_erase_count = 0x1234U;
    assert(mram_store_save_service_data(&service) == BOARD_OK);

    /* Simulate active hardware before shutdown. */
    assert(board_nand_power_on(1U) == BOARD_OK);
    assert(board_nand_power_on(2U) == BOARD_OK);
    ctx.ped.is_powered = true;
    ctx.nand1.is_powered = true;
    ctx.nand2.is_powered = true;
    ctx.alarm_status = 0x0005UL;
    ctx.nand1.is_full = true;
    ctx.test.result_status = 0x00000002UL;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_SHUTDOWN;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_SHUTDOWN);
    assert(ctx.shutdown.source_state == STATE_DUTY);
    assert(ctx.shutdown.stage == SHUTDOWN_STAGE_DONE);
    assert(!ctx.shutdown.service_data_save_failed);
    assert(!ctx.shutdown.power_off_failed);
    assert(!ctx.ped.is_powered);

    assert((board_nand_is_powered(1U, &powered1) == BOARD_OK) && (powered1 == 0U));
    assert((board_nand_is_powered(2U, &powered2) == BOARD_OK) && (powered2 == 0U));

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.alarm_status == 0x0005U);
    assert(service.nand1_full == 1U);
    assert(service.nand2_full == 0U);
    assert(service.last_test_status == 0x00000002UL);
    assert(service.nand1_erase_count == 0x1234U); /* preserved, not overwritten */
}

/* Entry into SHUTDOWN is also allowed from ALARM. */
static void shutdown_from_alarm_transitions(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_ALARM);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_SHUTDOWN;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_SHUTDOWN);
    assert(ctx.shutdown.source_state == STATE_ALARM);
}

/* A failed MRAM service-data save is recorded as non-nominal but must not stop
 * the shutdown: the mode still reaches SHUTDOWN. */
static void shutdown_records_save_failure_but_completes(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUTY);

    board_stub_set_mram_write_fail(true);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_SHUTDOWN;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    board_stub_set_mram_write_fail(false);

    assert(ctx.state == STATE_SHUTDOWN);
    assert(ctx.shutdown.service_data_save_failed);
    assert(!ctx.shutdown.power_off_failed);
}

/* CMD_STATUS_REQ is the only KU accepted in SHUTDOWN: it is answered with a
 * status TS and does not change the mode. */
static void status_req_allowed_in_shutdown(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_SHUTDOWN);

    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* enqueue event */
    algorithm_process_events(&ctx);               /* enqueue ACK + status */
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush both (status last) */

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, &length));
    assert(message_id == TS_STATUS_MSG_ID);
    assert(ctx.state == STATE_SHUTDOWN);
}

/* A forbidden KU (well-formed CMD_ERASE) is rejected with ERR_MODE and leaves
 * the mode unchanged. */
static void forbidden_command_rejected_with_err_mode(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_SHUTDOWN);

    /* Valid erase payload (NAND1, power off): builds successfully so the FSM,
     * not the parser, is what rejects it. */
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    payload[0] = 0x01U;
    board_comm_stub_inject_rx(KU_ERASE_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* enqueue event */
    algorithm_process_events(&ctx);               /* reject -> ERR_MODE ACK */
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush the ACK */

    assert(board_comm_stub_last_tx(&message_id, &address_to, reply, sizeof(reply),
                                   &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(reply[0] == (uint8_t)(KU_ERASE_MSG_ID & 0xFFU));
    assert(reply[1] == (uint8_t)(KU_ERASE_MSG_ID >> 8U));
    assert(reply[2] == ACK_ERR_MODE);
    assert(ctx.state == STATE_SHUTDOWN);
}

/* KT (telemetry) messages are ignored in SHUTDOWN with no quittance. */
static void telemetry_ignored_in_shutdown(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_SHUTDOWN);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_ORBIT;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_SHUTDOWN);
    assert(board_comm_stub_tx_count() == 0U);
}

int main(void) {
    shutdown_from_duty_powers_down_and_saves();
    shutdown_from_alarm_transitions();
    shutdown_records_save_failure_but_completes();
    status_req_allowed_in_shutdown();
    forbidden_command_rejected_with_err_mode();
    telemetry_ignored_in_shutdown();

    return 0;
}
