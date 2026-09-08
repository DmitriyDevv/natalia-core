#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "state.h"
#include "transport.h"

#define TS_STATUS_MSG_ID    (0x0D00U)
#define TS_ACK_MSG_ID       (0x0D01U)
#define TS_TELEMETRY_MSG_ID (0x0D02U)
#define TS_VERSION_MSG_ID   (0xFFE1U)

#define CMD_STATUS_MSG_ID      (0x0F01U)
#define CMD_TELEMETRY_MSG_ID   (0x0F00U)
#define CMD_SET_CFG_MSG_ID     (0x0F07U)
#define CMD_RESET_ALARM_MSG_ID (0x0F0DU)
#define CMD_VERSION_MSG_ID     (0xFFE1U)

#define TRANSPORT_FLUSH_POLLS (4U)
#define ACK_PAYLOAD_SIZE      (6U)
#define TELEMETRY_SIZE        (109U)
#define VERSION_SIZE          (3U)

static void begin_alarm_test(SystemContext* ctx) {
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_ALARM;
    ctx->previous_state = STATE_ALARM;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    board_stub_reset_all();
    system_event_queue_init();
    transport_reset();
}

static SystemEvent make_event(EventType type, uint32_t msg_id) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = msg_id;

    return event;
}

static void flush_transmit(SystemContext* ctx) {
    uint32_t index;

    for (index = 0U; index < TRANSPORT_FLUSH_POLLS; ++index) {
        assert(transport_poll(ctx, 0U) == BOARD_OK);
    }
}

static void assert_ack(uint32_t index, uint16_t command_id) {
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;
    uint8_t payload[ACK_PAYLOAD_SIZE];

    assert(board_comm_stub_tx_at(index, &message_id, &address_to, payload,
                                 sizeof(payload), &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(length == ACK_PAYLOAD_SIZE);
    assert(payload[0] == (uint8_t)(command_id & 0xFFU));
    assert(payload[1] == (uint8_t)(command_id >> 8U));
    assert(payload[2] == TRANSPORT_ACK_OK);
}

static void assert_message(uint32_t index, uint16_t expected_message_id,
                           uint16_t expected_length) {
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    assert(board_comm_stub_tx_at(index, &message_id, &address_to, NULL, 0U,
                                 &length));
    assert(message_id == expected_message_id);
    assert(length == expected_length);
}

static void assert_alarm_exit(const SystemContext* ctx) {
    assert(ctx->state == STATE_DUTY);
    assert(ctx->previous_state == STATE_ALARM);
    assert(ctx->masked_alarm == 0U);
}

static void reset_alarm_clears_maskable_flags_and_exits(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    ctx.alarm_status = ALARM_PU_TEMP;
    ctx.alarm_mask = ALARM_PU_TEMP;
    ctx.masked_alarm = ALARM_PU_TEMP;
    event = make_event(EVENT_CMD_RESET_ALARM, CMD_RESET_ALARM_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_DUTY);

    assert(ctx.alarm_status == 0U);
    assert_alarm_exit(&ctx);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_RESET_ALARM_MSG_ID);
    assert_message(1U, TS_STATUS_MSG_ID, ACK_PAYLOAD_SIZE);
}

static void reset_alarm_keeps_active_non_maskable_flag(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    ctx.alarm_status = ALARM_NAND_PS;
    ctx.alarm_mask = ALARM_NAND_PS;
    ctx.masked_alarm = ALARM_NAND_PS;
    event = make_event(EVENT_CMD_RESET_ALARM, CMD_RESET_ALARM_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_ALARM);

    assert(ctx.alarm_status == ALARM_NAND_PS);
    assert(ctx.masked_alarm == ALARM_NAND_PS);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_RESET_ALARM_MSG_ID);
}

static void set_cfg_masking_active_alarm_exits_to_duty(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    ctx.alarm_status = ALARM_PU_TEMP;
    ctx.alarm_mask = ALARM_PU_TEMP;
    ctx.masked_alarm = ALARM_PU_TEMP;
    event = make_event(EVENT_CMD_SET_CFG, CMD_SET_CFG_MSG_ID);
    event.command.set_config.alarm_mask = 0U;

    assert(handle_event(&ctx, &event) == STATE_DUTY);

    assert((ctx.alarm_mask & ALARM_PU_TEMP) == 0U);
    assert_alarm_exit(&ctx);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_SET_CFG_MSG_ID);
    assert_message(1U, TS_STATUS_MSG_ID, ACK_PAYLOAD_SIZE);
}

static void set_cfg_not_masking_active_alarm_stays_in_alarm(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    ctx.alarm_status = ALARM_PU_TEMP;
    ctx.alarm_mask = ALARM_PU_TEMP;
    ctx.masked_alarm = ALARM_PU_TEMP;
    event = make_event(EVENT_CMD_SET_CFG, CMD_SET_CFG_MSG_ID);
    event.command.set_config.alarm_mask = ALARM_PU_TEMP;

    assert(handle_event(&ctx, &event) == STATE_ALARM);

    assert(ctx.masked_alarm == ALARM_PU_TEMP);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_SET_CFG_MSG_ID);
}

static void set_cfg_cannot_unmask_non_maskable_alarm(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    ctx.alarm_status = ALARM_NAND_PS;
    ctx.alarm_mask = ALARM_NAND_PS;
    ctx.masked_alarm = ALARM_NAND_PS;
    event = make_event(EVENT_CMD_SET_CFG, CMD_SET_CFG_MSG_ID);
    event.command.set_config.alarm_mask = 0U;

    assert(handle_event(&ctx, &event) == STATE_ALARM);

    assert((ctx.alarm_mask & ALARM_NAND_PS) != 0U);
    assert(ctx.masked_alarm == ALARM_NAND_PS);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_SET_CFG_MSG_ID);
}

static void masked_alarm_clear_exits_only_when_no_alarm_remains(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    event = make_event(EVENT_MASKED_ALARM_CLEAR, 0U);

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    assert_alarm_exit(&ctx);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_message(0U, TS_STATUS_MSG_ID, ACK_PAYLOAD_SIZE);

    begin_alarm_test(&ctx);
    ctx.masked_alarm = ALARM_PU_TEMP;
    event = make_event(EVENT_MASKED_ALARM_CLEAR, 0U);

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 0U);
}

static void telemetry_request_answers_with_ack_and_telemetry(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    event = make_event(EVENT_CMD_TELEM_REQ, CMD_TELEMETRY_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    flush_transmit(&ctx);

    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_TELEMETRY_MSG_ID);
    assert_message(1U, TS_TELEMETRY_MSG_ID, TELEMETRY_SIZE);
}

static void status_and_version_requests_answer_in_alarm(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_alarm_test(&ctx);
    event = make_event(EVENT_CMD_STATUS_REQ, CMD_STATUS_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_STATUS_MSG_ID);
    assert_message(1U, TS_STATUS_MSG_ID, ACK_PAYLOAD_SIZE);

    begin_alarm_test(&ctx);
    event = make_event(EVENT_CMD_VERSION_REQ, CMD_VERSION_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_VERSION_MSG_ID);
    assert_message(1U, TS_VERSION_MSG_ID, VERSION_SIZE);
}

int main(void) {
    reset_alarm_clears_maskable_flags_and_exits();
    reset_alarm_keeps_active_non_maskable_flag();
    set_cfg_masking_active_alarm_exits_to_duty();
    set_cfg_not_masking_active_alarm_stays_in_alarm();
    set_cfg_cannot_unmask_non_maskable_alarm();
    masked_alarm_clear_exits_only_when_no_alarm_remains();
    telemetry_request_answers_with_ack_and_telemetry();
    status_and_version_requests_answer_in_alarm();

    return 0;
}
