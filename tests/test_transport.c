#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "test_mode_config.h"
#include "transport.h"

#define KU_STATUS_REQ_MSG_ID   (0x0F01U)
#define KU_SPUTNIKS_TIME_ID    (0x0401U)
#define KU_TEST_RESULT_MSG_ID  (0x0F0AU)
#define KU_VERSION_REQ_MSG_ID  (0xFFE0U)
#define KU_SET_DEST_ID_MSG_ID  (0x0A61U)
#define KU_SET_DEVICE_ID_MSG_ID (0x0A62U)
#define KU_OBSERVE_START_MSG_ID (0x0F03U)
#define KU_ERASE_MSG_ID        (0x0F08U)
#define TS_ACK_MSG_ID          (0x0D01U)
#define TS_TELEMETRY_MSG_ID    (0x0D02U)
#define TS_TEST_RESULT_MSG_ID  (0x0D03U)
#define TS_VERSION_MSG_ID      (0xFFE1U)
#define TELEMETRY_SIZE         (109U)
#define UNKNOWN_MSG_ID         (0x0FFFU)
#define SHORT_PAYLOAD_SIZE   (6U)
#define FILL_BYTE            (0xAAU)
#define ADDR_NA              (0x1EU)
#define ADDR_BVS             (0x05U)
#define ADDR_OTHER           (0x03U)
#define CAN_CTRL_DEST_SENDER (0x0001U)
#define CAN_CTRL_IGN_SPUTNIK (0x0002U)
#define ACK_OK               (0x00U)
#define ACK_ERR_CONTENT      (0x05U)
#define TS_VERSION_SIZE      (3U)

static void make_fill_payload(uint8_t* payload) {
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
}

static void known_command_is_parsed_and_enqueued(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA, NULL, 0U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_STATUS_REQ);
    assert(event.msg_id == KU_STATUS_REQ_MSG_ID);
}

static void expect_content_error_ack(SystemContext* ctx, uint16_t command_id) {
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t length = 0U;

    assert(transport_poll(ctx, 0U) == BOARD_OK);
    assert(transport_poll(ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
    assert(board_comm_stub_last_tx(&message_id, NULL, reply, sizeof(reply), &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(reply[0] == (uint8_t)(command_id & 0xFFU));
    assert(reply[1] == (uint8_t)(command_id >> 8U));
    assert(reply[2] == ACK_ERR_CONTENT);
}

/* CAN v2.1 §2: KU lengths are exact. A zero-length KU with data and a v2-style
 * short KU padded to 6 bytes with AAh are rejected with ACK 05h. */
static void short_command_length_must_match_v2_1(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_VERSION_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);
    expect_content_error_ack(&ctx, KU_VERSION_REQ_MSG_ID);

    payload[0] = 0x01U; /* NAND1, padded to 6 bytes as in v2 */
    board_comm_stub_inject_rx(KU_ERASE_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);
    expect_content_error_ack(&ctx, KU_ERASE_MSG_ID);

    board_comm_stub_inject_rx(KU_SET_DEST_ID_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);
    expect_content_error_ack(&ctx, KU_SET_DEST_ID_MSG_ID);

    board_comm_stub_inject_rx(KU_ERASE_MSG_ID, ADDR_BVS, ADDR_NA, payload, 1U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);
}

/* End to end in DUTY: version request is acked and answered with TS 0xFFE1
 * carrying the placeholder major/minor/extra version bytes. */
static void version_request_replies_with_version_ts(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t got[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    board_comm_stub_inject_rx(KU_VERSION_REQ_MSG_ID, ADDR_BVS, ADDR_NA, NULL, 0U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_pop(&event));
    assert(handle_event(&ctx, &event) == STATE_DUTY);

    for (i = 0; i < 4; ++i) {
        assert(transport_poll(&ctx, 0U) == BOARD_OK);
    }

    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got),
                                   &length));
    assert(message_id == TS_VERSION_MSG_ID);
    assert(length == TS_VERSION_SIZE);
    assert(got[0] == 255U);
    assert(got[1] == 255U);
    assert(got[2] == 255U);
}

/* §2.16: setting the Device ID in a normal mode persists it to MRAM while
 * preserving the stored Destination ID and the rest of the config image. */
static void set_device_id_persists_to_mram(void) {
    SystemContext ctx;
    MramStoreConfig baseline;
    MramStoreConfig loaded;
    uint8_t payload[2];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&baseline, 0, sizeof(baseline));
    baseline.config_version = 7U;
    baseline.device_id = 0x00AAU;
    baseline.destination_id = 0x00BBU;
    assert(mram_store_save_config(&baseline) == BOARD_OK);
    transport_apply_stored_addresses(baseline.device_id, baseline.destination_id);

    payload[0] = 0x42U;
    payload[1] = 0x00U; /* device_id = 0x0042 */
    board_comm_stub_inject_rx(KU_SET_DEVICE_ID_MSG_ID, ADDR_BVS, 0x00AAU, payload, 2U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    memset(&loaded, 0, sizeof(loaded));
    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.device_id == 0x0042U);
    assert(loaded.destination_id == 0x00BBU); /* preserved */
    assert(loaded.config_version == 7U);      /* preserved */
}

/* §5.2.9 note 1: in SHUTDOWN the Device/Destination ID changes RAM only; MRAM
 * must not be rewritten. */
static void set_device_id_in_shutdown_is_ram_only(void) {
    SystemContext ctx;
    MramStoreConfig baseline;
    MramStoreConfig loaded;
    uint8_t payload[2];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_SHUTDOWN;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&baseline, 0, sizeof(baseline));
    baseline.config_version = 3U;
    baseline.device_id = 0x00AAU;
    baseline.destination_id = 0x00BBU;
    assert(mram_store_save_config(&baseline) == BOARD_OK);
    transport_apply_stored_addresses(baseline.device_id, baseline.destination_id);

    payload[0] = 0x42U;
    payload[1] = 0x00U;
    board_comm_stub_inject_rx(KU_SET_DEVICE_ID_MSG_ID, ADDR_BVS, 0x00AAU, payload, 2U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    memset(&loaded, 0, sizeof(loaded));
    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.device_id == 0x00AAU); /* unchanged in MRAM */
}

static uint8_t inject_address_command(SystemContext* ctx, uint16_t command_id,
                                      uint16_t address_to, uint16_t value) {
    uint8_t payload[2];
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;

    payload[0] = (uint8_t)(value & 0xFFU);
    payload[1] = (uint8_t)(value >> 8U);
    board_comm_stub_inject_rx(command_id, ADDR_BVS, address_to, payload, 2U);
    assert(transport_poll(ctx, 0U) == BOARD_OK);
    assert(transport_poll(ctx, 0U) == BOARD_OK);
    assert(board_comm_stub_last_tx(&message_id, NULL, reply, sizeof(reply), NULL));
    assert(message_id == TS_ACK_MSG_ID);
    return reply[2];
}

/* UniCAN 29-bit identifiers carry 14-bit addresses: Destination/Device ID
 * above 3FFFh is rejected with ACK 05h and neither RAM nor MRAM changes. */
static void address_above_14_bits_is_rejected(void) {
    SystemContext ctx;
    MramStoreConfig baseline;
    MramStoreConfig loaded;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&baseline, 0, sizeof(baseline));
    baseline.device_id = ADDR_NA;
    baseline.destination_id = ADDR_BVS;
    assert(mram_store_save_config(&baseline) == BOARD_OK);

    assert(inject_address_command(&ctx, KU_SET_DEST_ID_MSG_ID, ADDR_NA, 0x4000U) ==
           ACK_ERR_CONTENT);
    assert(inject_address_command(&ctx, KU_SET_DEVICE_ID_MSG_ID, ADDR_NA, 0xFFFFU) ==
           ACK_ERR_CONTENT);

    memset(&loaded, 0, sizeof(loaded));
    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.device_id == ADDR_NA);
    assert(loaded.destination_id == ADDR_BVS);
    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, NULL));
    assert(address_to == ADDR_BVS);

    assert(inject_address_command(&ctx, KU_SET_DEVICE_ID_MSG_ID, ADDR_NA, 0x3FFFU) ==
           ACK_OK);
    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.device_id == 0x3FFFU);
}

/* An out-of-range address already in MRAM is not applied at boot. */
static void stored_address_above_14_bits_is_ignored(void) {
    SystemContext ctx;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    transport_apply_stored_addresses(0x4000U, 0x8005U);
    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA, NULL, 0U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);

    board_comm_stub_inject_rx(UNKNOWN_MSG_ID, ADDR_BVS, ADDR_NA, NULL, 0U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, NULL));
    assert(address_to == ADDR_BVS);
}

/* Boot load: stored Destination ID is applied so replies target it. */
static void stored_addresses_applied_on_load(void) {
    SystemContext ctx;
    MramStoreConfig baseline;
    MramStoreServiceData service;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&baseline, 0, sizeof(baseline));
    baseline.config_version = 1U;
    baseline.device_id = 0x0071U;
    baseline.destination_id = 0x0033U;
    assert(mram_store_save_config(&baseline) == BOARD_OK);

    memset(&service, 0, sizeof(service));
    assert(mram_store_save_service_data(&service) == BOARD_OK);

    assert(action_load_mram(&ctx) == ACTION_OK);

    make_fill_payload(payload);
    board_comm_stub_inject_rx(UNKNOWN_MSG_ID, ADDR_BVS, 0x0071U,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, NULL));
    assert(address_to == 0x0033U);
}

static void message_for_other_node_is_dropped(void) {
    SystemContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_OTHER, NULL, 0U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

static void sputniks_time_ignored_when_can_control_bit_set(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    ctx.can_control = CAN_CTRL_IGN_SPUTNIK;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_SPUTNIKS_TIME_ID, ADDR_BVS, ADDR_NA, payload, 5U);

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
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_SPUTNIKS_TIME_ID, ADDR_BVS, ADDR_NA, payload, 5U);

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
    board_stub_reset_all();
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
    board_stub_reset_all();
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
    uint8_t payload[1];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    /* KU 000Ah hardware config: NAND2 (bits 0-1 = 2), MRAM copy 2 (bits 2-3 = 2). */
    payload[0] = 0x02U | (0x02U << 2U);

    board_comm_stub_inject_rx(KU_TEST_RESULT_MSG_ID, ADDR_BVS, ADDR_NA, payload, 1U);

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
    board_stub_reset_all();
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
    uint8_t payload[1];

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    /* MRAM copy field (bits 2-3) = 0 is invalid; command must be ignored. */
    payload[0] = 0x01U;

    board_comm_stub_inject_rx(KU_TEST_RESULT_MSG_ID, ADDR_BVS, ADDR_NA, payload, 1U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

/* §9.11: a bank that was never tested has no valid result -> the action must
 * answer ERR_OTHER and send no TS content. */
static void test_result_invalid_bank_replies_err_other(void) {
    SystemContext ctx;
    uint16_t message_id = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    board_stub_set_test_result_valid(1U, false);
    assert(action_send_test_result(NAND_BANK_1, 1U) == ACTION_ERR_OTHER);
    board_stub_set_test_result_valid(1U, true);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(!board_comm_stub_last_tx(&message_id, NULL, NULL, 0U, NULL));
}

static void inject_observe_start(uint16_t observe_params) {
    uint8_t payload[5];

    payload[0] = 0x01U; /* bank NAND1, all other config bits 0 */
    payload[1] = (uint8_t)(observe_params & 0xFFU);
    payload[2] = (uint8_t)((observe_params >> 8U) & 0xFFU);
    payload[3] = 0x00U; /* trigger config */
    payload[4] = 0x00U;
    board_comm_stub_inject_rx(KU_OBSERVE_START_MSG_ID, ADDR_BVS, ADDR_NA, payload, 5U);
}

/* §9.4 consistency rules for the observe parameter word. */
static void observe_params_validation(void) {
    SystemContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;

    /* invalid: event format on (mode 1) but event count = 0 */
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    inject_observe_start(0x0001U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);

    /* invalid: Спектр-1 selected but Nhist = 0 */
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    inject_observe_start(0x0040U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);

    /* valid: mode 7 (outside belts by the B_lim(L) curve), count 1 */
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    inject_observe_start(0x000FU);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);

    /* invalid: mode 7 but event count = 0 */
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    inject_observe_start(0x0007U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);

    /* valid: events (mode 1, count 1) + Спектр-1 with Nhist = 256 */
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
    inject_observe_start(0x0149U);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

/* TS «Телеметрия» 0D02h: 109-byte frame assembled from live reads, ctx, the
 * MRAM config/service image and the SW version. Spot-checks representative
 * fields across all four sources. */
static void telemetry_frame_0d02(void) {
    SystemContext ctx;
    MramStoreConfig cfg;
    MramStoreServiceData svc;
    uint8_t got[TELEMETRY_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    ctx.alarm_status = 0x00A5U;
    ctx.masked_alarm = 0x0084U;
    ctx.nand1.is_full = true;
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    board_stub_set_rtc_time(0x11223344UL, 341U);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 30000, true); /* 30 C -> 3000 (0.01 C) */
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PU, 3300U, 0, true);

    memset(&cfg, 0, sizeof(cfg));
    cfg.config_version = 9U;
    cfg.mcu_pu_temp_min = -40;
    cfg.pu_voltage_min = 3000U;
    cfg.init_rtc_time_ms = 0x0202U;
    cfg.init_rtc_time = 0x0A0B0C0DUL;
    cfg.alarm_mask = 0x00FFU;
    cfg.can_control = 0x0003U;
    cfg.destination_id = 0x0033U;
    cfg.device_id = 0x0071U;
    assert(mram_store_save_config(&cfg) == BOARD_OK);

    memset(&svc, 0, sizeof(svc));
    svc.observe_session_id = 0xABCDU;
    svc.nand1_packet_count = 0x123456UL;
    svc.nand1_erase_count = 0x1111U;
    assert(mram_store_save_service_data(&svc) == BOARD_OK);

    assert(action_send_telem(&ctx) == ACTION_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush the single long TX */

    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got), &length));
    assert(message_id == TS_TELEMETRY_MSG_ID);
    assert(length == TELEMETRY_SIZE);

    assert(rd16(&got[0]) == 341U);               /* RTC ms */
    assert(got[2] == 0x44U && got[5] == 0x11U);  /* RTC sec LE */
    assert(rd16(&got[8]) == 3000U);              /* PU temp (0.01 C) */
    assert(rd16(&got[14]) == 3300U);             /* PU voltage mV */
    assert(rd16(&got[22]) == 0x00A5U);           /* alarm status */
    assert(rd16(&got[24]) == 0x0084U);           /* masked alarm */
    assert(got[26] == 0x08U);                    /* mode: prev INIT, curr DUTY */
    assert(got[27] == 0x01U);                    /* NAND1 full */
    assert(rd16(&got[36]) == 0xFFD8U);           /* mcu_pu_temp_min = -40 */
    assert(rd16(&got[52]) == 3000U);             /* pu_voltage_min */
    assert(rd16(&got[76]) == 0x0202U);           /* init RTC ms */
    assert(got[78] == 0x0DU && got[81] == 0x0AU); /* init RTC sec LE */
    assert(rd16(&got[82]) == 0xABCDU);           /* observe session id */
    assert(got[84] == 0x56U && got[86] == 0x12U); /* NAND1 packet u24 LE */
    assert(rd16(&got[90]) == 0x1111U);           /* NAND1 erase count */
    assert(rd16(&got[98]) == 0x00FFU);           /* alarm mask */
    assert(rd16(&got[100]) == 0x0003U);          /* can control */
    assert(rd16(&got[102]) == 0x0033U);          /* destination id */
    assert(rd16(&got[104]) == 0x0071U);          /* device id */
    assert(got[106] == 255U && got[107] == 255U && got[108] == 255U);
}

int main(void) {
    known_command_is_parsed_and_enqueued();
    test_result_invalid_bank_replies_err_other();
    observe_params_validation();
    telemetry_frame_0d02();
    short_command_length_must_match_v2_1();
    version_request_replies_with_version_ts();
    set_device_id_persists_to_mram();
    set_device_id_in_shutdown_is_ram_only();
    stored_addresses_applied_on_load();
    address_above_14_bits_is_rejected();
    stored_address_above_14_bits_is_ignored();
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
