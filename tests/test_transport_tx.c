#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

/* T-08: the transport TX queue (8 messages) and its retries (3 after the first
 * attempt). Telemetry is stored in its queue slot, so back-to-back telemetry
 * requests are all answered; only the 6146-byte test result uses the single long
 * buffer. Also: messages for another address are dropped once the Device ID has
 * changed, and the PED fields of TS "Telemetry" stay zero while the PED is off. */

#define TS_STATUS_MSG_ID      (0x0D00U)
#define TS_ACK_MSG_ID         (0x0D01U)
#define TS_TELEMETRY_MSG_ID   (0x0D02U)
#define TS_TEST_RESULT_MSG_ID (0x0D03U)
#define KU_STATUS_REQ_ID      (0x0F01U)
#define ADDR_NA               (0x1EU)
#define ADDR_BVS              (0x05U)
#define QUEUE_LENGTH          (8U)
#define POLLS                 (64U)

static uint8_t long_image[BOARD_MRAM_TEST_RESULT_IMAGE_SIZE];

static void begin(SystemContext *ctx) {
    MramStoreConfig config;
    MramStoreServiceData service;

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&config, 0, sizeof(config));
    assert(mram_store_save_config(&config) == BOARD_OK);
    memset(&service, 0, sizeof(service));
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static void flush(SystemContext *ctx) {
    uint32_t i;

    for (i = 0U; i < POLLS; ++i) {
        (void)transport_poll(ctx, 0U);
    }
}

static uint16_t tx_id(uint32_t index) {
    uint16_t message_id = 0U;

    assert(board_comm_stub_tx_at(index, &message_id, NULL, NULL, 0U, NULL));
    return message_id;
}

static uint16_t ack_command(uint32_t index) {
    uint16_t message_id = 0U;
    uint8_t payload[6];

    assert(board_comm_stub_tx_at(index, &message_id, NULL, payload, sizeof(payload), NULL));
    assert(message_id == TS_ACK_MSG_ID);
    return (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8U));
}

static void ack_goes_before_status(void) {
    SystemContext ctx;
    SystemEvent event;

    begin(&ctx);
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_ERASE;
    event.msg_id = 0x0F08U;
    event.command.erase.bank = NAND_BANK_1;
    (void)handle_event(&ctx, &event);
    flush(&ctx);

    assert(board_comm_stub_tx_count() == 2U);
    assert(tx_id(0U) == TS_ACK_MSG_ID);
    assert(tx_id(1U) == TS_STATUS_MSG_ID);
}

static void failed_send_is_retried(void) {
    SystemContext ctx;

    begin(&ctx);
    board_comm_stub_set_send_fail(3U);
    assert(transport_send_ack(0x0F01U, TRANSPORT_ACK_OK) == BOARD_OK);
    flush(&ctx);

    assert(board_comm_stub_tx_count() == 1U);
    assert(ack_command(0U) == 0x0F01U);
}

static void message_dropped_after_retries_and_queue_moves_on(void) {
    SystemContext ctx;

    begin(&ctx);
    board_comm_stub_set_send_fail(4U);
    assert(transport_send_ack(0x0F01U, TRANSPORT_ACK_OK) == BOARD_OK);
    assert(transport_send_ack(0x0F02U, TRANSPORT_ACK_OK) == BOARD_OK);
    flush(&ctx);

    assert(board_comm_stub_tx_count() == 1U);
    assert(ack_command(0U) == 0x0F02U);
}

static void full_queue_keeps_order_and_rejects_extra(void) {
    SystemContext ctx;
    uint32_t i;

    begin(&ctx);
    for (i = 0U; i < QUEUE_LENGTH; ++i) {
        assert(transport_send_ack((uint16_t)(0x0F00U + i), TRANSPORT_ACK_OK) == BOARD_OK);
    }
    assert(transport_send_ack(0x0FFFU, TRANSPORT_ACK_OK) == BOARD_ERR_BUSY);

    flush(&ctx);
    assert(board_comm_stub_tx_count() == QUEUE_LENGTH);
    for (i = 0U; i < QUEUE_LENGTH; ++i) {
        assert(ack_command(i) == (uint16_t)(0x0F00U + i));
    }
}

static void long_buffer_is_single_and_released(void) {
    SystemContext ctx;
    uint16_t length = 0U;
    uint16_t message_id = 0U;

    begin(&ctx);
    memset(long_image, 0x5A, sizeof(long_image));
    assert(transport_send_test_result(long_image, (uint16_t)sizeof(long_image)) == BOARD_OK);
    assert(transport_send_test_result(long_image, (uint16_t)sizeof(long_image)) == BOARD_ERR_BUSY);

    flush(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert(board_comm_stub_tx_at(0U, &message_id, NULL, NULL, 0U, &length));
    assert(message_id == TS_TEST_RESULT_MSG_ID);
    assert(length == BOARD_MRAM_TEST_RESULT_IMAGE_SIZE);

    assert(transport_send_test_result(long_image, (uint16_t)sizeof(long_image)) == BOARD_OK);
    flush(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
}

static void back_to_back_telemetry_is_all_sent(void) {
    SystemContext ctx;

    begin(&ctx);
    memset(long_image, 0x11, sizeof(long_image));
    assert(transport_send_test_result(long_image, (uint16_t)sizeof(long_image)) == BOARD_OK);
    assert(transport_send_telemetry(&ctx) == BOARD_OK);
    assert(transport_send_telemetry(&ctx) == BOARD_OK);
    flush(&ctx);

    assert(board_comm_stub_tx_count() == 3U);
    assert(tx_id(0U) == TS_TEST_RESULT_MSG_ID);
    assert(tx_id(1U) == TS_TELEMETRY_MSG_ID);
    assert(tx_id(2U) == TS_TELEMETRY_MSG_ID);
}

static void old_address_is_dropped_after_device_id_change(void) {
    SystemContext ctx;
    uint8_t payload[6];
    SystemEvent event;

    begin(&ctx);
    transport_apply_stored_addresses(0x0042U, ADDR_BVS);
    memset(payload, 0xAA, sizeof(payload));

    board_comm_stub_inject_rx(KU_STATUS_REQ_ID, ADDR_BVS, ADDR_NA, payload, sizeof(payload));
    (void)transport_poll(&ctx, 0U);
    assert(system_event_queue_get_count() == 0U);

    board_comm_stub_inject_rx(KU_STATUS_REQ_ID, ADDR_BVS, 0x0042U, payload, sizeof(payload));
    (void)transport_poll(&ctx, 0U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_STATUS_REQ);
}

static void read_telemetry(uint8_t *frame) {
    uint32_t total = board_comm_stub_tx_count();
    uint16_t message_id = 0U;

    assert(total > 0U);
    assert(board_comm_stub_tx_at(total - 1U, &message_id, NULL, frame, 109U, NULL));
    assert(message_id == TS_TELEMETRY_MSG_ID);
}

static void ped_fields_zero_while_ped_off(void) {
    SystemContext ctx;
    uint8_t frame[109];

    begin(&ctx);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PED, 30000, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PED, 5000U, 200000, true);

    ctx.ped.is_powered = false;
    assert(transport_send_telemetry(&ctx) == BOARD_OK);
    flush(&ctx);
    read_telemetry(frame);
    assert((frame[10] | frame[11]) == 0U);
    assert((frame[18] | frame[19]) == 0U);
    assert((frame[20] | frame[21]) == 0U);
    assert((frame[8] | frame[9]) != 0U);

    ctx.ped.is_powered = true;
    assert(transport_send_telemetry(&ctx) == BOARD_OK);
    flush(&ctx);
    read_telemetry(frame);
    assert(frame[10] == (uint8_t)(300U & 0xFFU));
    assert((frame[18] | ((uint16_t)frame[19] << 8U)) == 5000U);
    assert((frame[20] | ((uint16_t)frame[21] << 8U)) == 200U);
}

int main(void) {
    ack_goes_before_status();
    failed_send_is_retried();
    message_dropped_after_retries_and_queue_moves_on();
    full_queue_keeps_order_and_rejects_extra();
    long_buffer_is_single_and_released();
    back_to_back_telemetry_is_all_sent();
    old_address_is_dropped_after_device_id_change();
    ped_fields_zero_while_ped_off();

    return 0;
}
