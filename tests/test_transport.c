#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "test_mode_config.h"
#include "transport.h"

#define KU_STATUS_REQ_MSG_ID   (0x0001U)
#define KU_SPUTNIKS_TIME_ID    (0x0401U)
#define KU_TEST_RESULT_MSG_ID  (0x000AU)
#define TS_ACK_MSG_ID          (0x0201U)
#define TS_TEST_RESULT_MSG_ID  (0x0203U)
#define UNKNOWN_MSG_ID         (0x0FFFU)
#define SHORT_PAYLOAD_SIZE   (6U)
#define FILL_BYTE            (0xAAU)
#define ADDR_NA              (0x1EU)
#define ADDR_BVS             (0x05U)
#define ADDR_OTHER           (0x03U)
#define CAN_CTRL_DEST_SENDER (0x0001U)
#define CAN_CTRL_IGN_SPUTNIK (0x0002U)

static void make_fill_payload(uint8_t* payload) {
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
}

static void known_command_is_parsed_and_enqueued(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_STATUS_REQ);
    assert(event.msg_id == KU_STATUS_REQ_MSG_ID);
}

static void message_for_other_node_is_dropped(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_OTHER,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

static void sputniks_time_ignored_when_can_control_bit_set(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.can_control = CAN_CTRL_IGN_SPUTNIK;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_SPUTNIKS_TIME_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
    assert(board_comm_stub_tx_count() == 0U);
}

static void sputniks_time_processed_when_can_control_bit_clear(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.can_control = 0U;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_SPUTNIKS_TIME_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_SET_TIME);
}

static void reply_targets_sender_when_can_control_bit_set(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.can_control = CAN_CTRL_DEST_SENDER;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(UNKNOWN_MSG_ID, ADDR_OTHER, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* parse + enqueue ack + send */
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush the sent item */

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, NULL));
    assert(message_id == TS_ACK_MSG_ID);
    assert(address_to == ADDR_OTHER);
}

static void reply_targets_stored_address_when_can_control_bit_clear(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.can_control = 0U;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(UNKNOWN_MSG_ID, ADDR_OTHER, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, NULL));
    assert(message_id == TS_ACK_MSG_ID);
    assert(address_to == ADDR_BVS);
}

static void test_result_request_decodes_bank_and_mram_copy(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    /* KU 000Ah hardware config: NAND2 (bits 0-1 = 2), MRAM copy 2 (bits 2-3 = 2). */
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    payload[0] = 0x02U | (0x02U << 2U);

    board_comm_stub_inject_rx(KU_TEST_RESULT_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_TEST_RESULT);
    assert(event.msg_id == KU_TEST_RESULT_MSG_ID);
    assert(event.command.test_result.bank == NAND_BANK_2);
    assert(event.command.test_result.mram_copy == 2U);
}

/* The action reads the raw 6146-byte image (data + CRC copied from MRAM) and
 * hands it to transport as one long TS 0203h. Driven directly so the message
 * under test is the only queued TX (the full-FSM path also appends an ACK, and
 * the synchronous host stub would flush both within a single poll). */
static void test_result_action_sends_long_ts_0203(void) {
    SystemContext ctx;
    static MramStoreTestResult stored;
    static uint8_t expected[BOARD_MRAM_TEST_RESULT_IMAGE_SIZE];
    static uint8_t got[BOARD_MRAM_TEST_RESULT_IMAGE_SIZE];
    uint8_t is_valid = 0U;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    /* Persist a known per-bank Nerr image on both MRAM copies. */
    memset(&stored, 0, sizeof(stored));
    stored.bank = 1U;
    stored.nerr[0] = 0x00000123UL;
    stored.nerr[1] = 0x00ABCDEFUL;
    stored.nerr[TEST_MODE_BLOCK_COUNT - 1U] = 0x00FFFFFFUL;
    assert(mram_store_save_test_result(&stored) == BOARD_OK);

    /* Expected wire image = stored data (6144) + CRC copied from MRAM copy 1. */
    assert(board_mram_read_test_result(1U, 1U, expected,
                                       BOARD_MRAM_TEST_RESULT_SIZE, &is_valid,
                                       &expected[BOARD_MRAM_TEST_RESULT_SIZE]) == BOARD_OK);

    assert(action_send_test_result(NAND_BANK_1, 1U) == ACTION_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush the single long TX */

    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got), &length));
    assert(message_id == TS_TEST_RESULT_MSG_ID);
    assert(length == (uint16_t)BOARD_MRAM_TEST_RESULT_IMAGE_SIZE);
    assert(address_to == ADDR_BVS);
    assert(memcmp(got, expected, BOARD_MRAM_TEST_RESULT_IMAGE_SIZE) == 0);

    /* 24-bit little-endian block error counts land where the format requires. */
    assert((got[0] == 0x23U) && (got[1] == 0x01U) && (got[2] == 0x00U));
    assert((got[3] == 0xEFU) && (got[4] == 0xCDU) && (got[5] == 0xABU));
    assert((got[6141] == 0xFFU) && (got[6142] == 0xFFU) && (got[6143] == 0xFFU));
}

static void test_result_request_rejects_invalid_mram_copy(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    /* MRAM copy field (bits 2-3) = 0 is invalid; command must be ignored. */
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    payload[0] = 0x01U;

    board_comm_stub_inject_rx(KU_TEST_RESULT_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

int main(void) {
    known_command_is_parsed_and_enqueued();
    message_for_other_node_is_dropped();
    sputniks_time_ignored_when_can_control_bit_set();
    sputniks_time_processed_when_can_control_bit_clear();
    reply_targets_sender_when_can_control_bit_set();
    reply_targets_stored_address_when_can_control_bit_clear();
    test_result_request_decodes_bank_and_mram_copy();
    test_result_action_sends_long_ts_0203();
    test_result_request_rejects_invalid_mram_copy();

    return 0;
}
