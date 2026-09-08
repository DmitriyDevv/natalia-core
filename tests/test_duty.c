#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "state.h"
#include "transport.h"

#define TS_STATUS_MSG_ID      (0x0D00U)
#define TS_ACK_MSG_ID         (0x0D01U)
#define TS_TELEMETRY_MSG_ID   (0x0D02U)
#define TS_TEST_RESULT_MSG_ID (0x0D03U)
#define TS_VERSION_MSG_ID     (0xFFE1U)

#define CMD_TELEMETRY_MSG_ID   (0x0F00U)
#define CMD_STATUS_MSG_ID      (0x0F01U)
#define CMD_SET_TIME_MSG_ID    (0x0F02U)
#define CMD_OBSERVE_MSG_ID     (0x0F03U)
#define CMD_DUTY_MSG_ID        (0x0F05U)
#define CMD_DUMP_MSG_ID        (0x0F06U)
#define CMD_ERASE_MSG_ID       (0x0F08U)
#define CMD_TEST_MSG_ID        (0x0F09U)
#define CMD_TEST_RESULT_MSG_ID (0x0F0AU)
#define CMD_VERSION_MSG_ID     (0xFFE0U)

#define ACK_PAYLOAD_SIZE (6U)
#define TELEMETRY_SIZE   (109U)
#define VERSION_SIZE     (3U)
#define FLUSH_POLLS      (4U)

static void begin_duty_test(SystemContext* ctx) {
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_DUTY;
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

    for (index = 0U; index < FLUSH_POLLS; ++index) {
        assert(transport_poll(ctx, 0U) == BOARD_OK);
    }
}

static void assert_ack(uint32_t index, uint16_t command_id,
                       TransportAckStatus expected_status) {
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
    assert(payload[2] == expected_status);
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

static void erase_and_test_start_with_command_bank(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_ERASE, CMD_ERASE_MSG_ID);
    event.command.erase.bank = NAND_BANK_2;
    event.command.erase.power_after_done = POWER_AFTER_DONE_KEEP;

    assert(handle_event(&ctx, &event) == STATE_ERASE);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.erase.bank == NAND_BANK_2);
    assert(ctx.erase.power_after_done == POWER_AFTER_DONE_KEEP);
    assert(ctx.erase.stage == ERASE_STAGE_WAIT);

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_TEST, CMD_TEST_MSG_ID);
    event.command.test.bank = NAND_BANK_1;
    event.command.test.power_after_done = POWER_AFTER_DONE_OFF;

    assert(handle_event(&ctx, &event) == STATE_TEST);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.test.bank == NAND_BANK_1);
    assert(ctx.test.power_after_done == POWER_AFTER_DONE_OFF);
    assert(ctx.test.stage == TEST_STAGE_ERASE);
}

static void dump_starts_for_usb_bank_and_packet_count(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_DUMP, CMD_DUMP_MSG_ID);
    event.command.dump.bank = NAND_BANK_2;
    event.command.dump.power_after_done = POWER_AFTER_DONE_KEEP;
    event.command.dump.start_address = 0U;
    event.command.dump.size = 3U * DUMP_MODE_PACKET_SIZE;
    event.command.dump.requested_packet_count = 3U;
    event.command.dump.dump_all = false;

    assert(handle_event(&ctx, &event) == STATE_DUMP);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.dump.bank == NAND_BANK_2);
    assert(ctx.dump.power_after_done == POWER_AFTER_DONE_KEEP);
    assert(ctx.dump.size == 3U * DUMP_MODE_PACKET_SIZE);
    assert(ctx.dump.stage == DUMP_STAGE_READ);
    assert(ctx.usb.is_ready);
}

static void observe_starts_and_full_bank_is_rejected_with_content_error(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_OBSERVE_START, CMD_OBSERVE_MSG_ID);
    event.command.observe_start.bank = NAND_BANK_1;
    event.command.observe_start.power_after_done = POWER_AFTER_DONE_KEEP;
    event.command.observe_start.observe_params = 0x0149U;
    event.command.observe_start.trigger_config = 0x0012U;
    event.command.observe_start.acquisition_period_ticks = 0x0149U;

    assert(handle_event(&ctx, &event) == STATE_OBSERVE);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.observe.bank == NAND_BANK_1);
    assert(ctx.observe.stage == OBSERVE_STAGE_ACTIVE);
    assert(ctx.observe.registration_enabled);

    begin_duty_test(&ctx);
    ctx.nand1.is_full = true;
    event = make_event(EVENT_CMD_OBSERVE_START, CMD_OBSERVE_MSG_ID);
    event.command.observe_start.bank = NAND_BANK_1;

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    assert(ctx.previous_state == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_OBSERVE_MSG_ID, TRANSPORT_ACK_ERR_CONTENT);
}

static void set_time_updates_rtc(void) {
    SystemContext ctx;
    SystemEvent event;
    InstrumentTime actual;

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_SET_TIME, CMD_SET_TIME_MSG_ID);
    event.command.set_time.time.milliseconds = 678U;
    event.command.set_time.time.seconds = 0x12345678U;

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    assert(board_rtc_get_time(&actual) == BOARD_OK);
    assert(actual.milliseconds == 678U);
    assert(actual.seconds == 0x12345678U);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_SET_TIME_MSG_ID, TRANSPORT_ACK_OK);
}

static void duty_status_telemetry_and_version_have_expected_formats(void) {
    SystemContext ctx;
    SystemEvent event;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;
    uint8_t status[ACK_PAYLOAD_SIZE];

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_STATUS_REQ, CMD_STATUS_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_STATUS_MSG_ID, TRANSPORT_ACK_OK);
    assert(board_comm_stub_tx_at(1U, &message_id, &address_to, status,
                                 sizeof(status), &length));
    assert(message_id == TS_STATUS_MSG_ID);
    assert(length == ACK_PAYLOAD_SIZE);
    assert(status[0] == 0x09U);

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_TELEM_REQ, CMD_TELEMETRY_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_TELEMETRY_MSG_ID, TRANSPORT_ACK_OK);
    assert_message(1U, TS_TELEMETRY_MSG_ID, TELEMETRY_SIZE);

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_VERSION_REQ, CMD_VERSION_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_ack(0U, CMD_VERSION_MSG_ID, TRANSPORT_ACK_OK);
    assert_message(1U, TS_VERSION_MSG_ID, VERSION_SIZE);
}

static void test_result_and_duty_command_answer_without_transition(void) {
    SystemContext ctx;
    SystemEvent event;
    static uint8_t result[BOARD_MRAM_TEST_RESULT_SIZE];

    begin_duty_test(&ctx);
    (void)memset(result, 0x5AU, sizeof(result));
    assert(board_mram_write_test_result(1U, 1U, result, sizeof(result)) == BOARD_OK);
    board_stub_set_test_result_valid(1U, true);
    event = make_event(EVENT_CMD_TEST_RESULT, CMD_TEST_RESULT_MSG_ID);
    event.command.test_result.bank = NAND_BANK_1;
    event.command.test_result.mram_copy = 1U;

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 2U);
    assert_message(0U, TS_TEST_RESULT_MSG_ID, BOARD_MRAM_TEST_RESULT_IMAGE_SIZE);
    assert_ack(1U, CMD_TEST_RESULT_MSG_ID, TRANSPORT_ACK_OK);

    begin_duty_test(&ctx);
    event = make_event(EVENT_CMD_DUTY, CMD_DUTY_MSG_ID);

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    assert(ctx.previous_state == STATE_DUTY);
    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert_ack(0U, CMD_DUTY_MSG_ID, TRANSPORT_ACK_OK);
}

static void masked_alarm_enters_alarm_with_safe_configuration(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t nand1_powered = 1U;
    uint8_t nand2_powered = 1U;

    begin_duty_test(&ctx);
    assert(board_nand_power_on(1U) == BOARD_OK);
    assert(board_nand_power_on(2U) == BOARD_OK);
    ctx.nand1.is_powered = true;
    ctx.nand2.is_powered = true;
    ctx.ped.is_powered = true;
    ctx.masked_alarm = ALARM_PU_TEMP;
    event = make_event(EVENT_MASKED_ALARM_SET, 0U);

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.ped.inhibit_enabled);
    assert(!ctx.ped.is_powered);
    assert(!ctx.nand1.is_powered);
    assert(!ctx.nand2.is_powered);
    assert((board_nand_is_powered(1U, &nand1_powered) == BOARD_OK) &&
           (nand1_powered == 0U));
    assert((board_nand_is_powered(2U, &nand2_powered) == BOARD_OK) &&
           (nand2_powered == 0U));
}

int main(void) {
    erase_and_test_start_with_command_bank();
    dump_starts_for_usb_bank_and_packet_count();
    observe_starts_and_full_bank_is_rejected_with_content_error();
    set_time_updates_rtc();
    duty_status_telemetry_and_version_have_expected_formats();
    test_result_and_duty_command_answer_without_transition();
    masked_alarm_enters_alarm_with_safe_configuration();

    return 0;
}
