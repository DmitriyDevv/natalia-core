#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "algorithm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

#define KU_DUMP_MSG_ID       (0x0006U)
#define KU_STATUS_REQ_MSG_ID (0x0001U)
#define KU_TEST_MSG_ID       (0x0009U)
#define TS_STATUS_MSG_ID     (0x0200U)
#define TS_ACK_MSG_ID        (0x0201U)
#define ACK_OK               (0x00U)
#define ACK_ERR_CONTENT      (0x05U)
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

static void enqueue_dump(NandBank bank, PowerAfterDone power_after_done,
                         bool dump_all, uint32_t packet_count) {
    SystemEvent event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.msg_id = KU_DUMP_MSG_ID;
    event.command.dump.bank = bank;
    event.command.dump.power_after_done = power_after_done;
    event.command.dump.dump_all = dump_all;
    event.command.dump.requested_packet_count = packet_count;
    event.command.dump.start_address = 0U;
    event.command.dump.size = dump_all ? 0U : (packet_count * DUMP_MODE_PACKET_SIZE);
    assert(system_event_queue_push_back(&event));
}

static void run_until_duty(SystemContext *ctx) {
    uint32_t guard;
    for (guard = 0U; (guard < 200U) && (ctx->state != STATE_DUTY); ++guard) {
        algorithm_poll(ctx);
        algorithm_process_events(ctx);
    }
}

/* Finding A: the CMD_DUMP quittance carries the packet count in bytes 5-7
 * (u24 LE), unlike every other ACK which fills them with 0xAA. */
static void dump_ack_reports_packet_count(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_DUMP);
    ctx.dump.size = 3U * DUMP_MODE_PACKET_SIZE;

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.msg_id = KU_DUMP_MSG_ID;

    assert(action_send_dump_ack(&ctx, &event) == ACTION_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, reply, sizeof(reply),
                                   &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(reply[0] == (uint8_t)(KU_DUMP_MSG_ID & 0xFFU));
    assert(reply[1] == (uint8_t)(KU_DUMP_MSG_ID >> 8U));
    assert(reply[2] == ACK_OK);
    assert(reply[3] == 0x03U); /* packet count = 3, u24 little-endian */
    assert(reply[4] == 0x00U);
    assert(reply[5] == 0x00U);
}

/* Fixed-count dump reads the requested packets over USB and returns to DUTY,
 * powering the bank off (power_after_done = OFF). */
static void fixed_count_dump_completes_to_duty(void) {
    SystemContext ctx;
    uint8_t powered = 1U;

    begin_test(&ctx, STATE_DUTY);

    enqueue_dump(NAND_BANK_1, POWER_AFTER_DONE_OFF, false, 2U);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUMP);

    run_until_duty(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.usb.bytes_written == 2U * DUMP_MODE_PACKET_SIZE);
    assert((board_nand_is_powered(1U, &powered) == BOARD_OK) && (powered == 0U));
}

/* Dump-all resolves the actual accumulated packet count from NAND and outputs
 * exactly that many packets; power_after_done = KEEP leaves the bank powered. */
static void dump_all_outputs_committed_packets(void) {
    SystemContext ctx;
    static uint8_t packet[DUMP_MODE_PACKET_SIZE];
    uint8_t powered = 0U;
    uint32_t i;

    begin_test(&ctx, STATE_DUTY);

    assert(board_nand_erase_start(1U) == BOARD_OK); /* reset committed count */
    memset(packet, 0x5AU, sizeof(packet));
    for (i = 0U; i < 3U; ++i) {
        assert(board_nand_write_packet(1U, packet) == BOARD_OK);
    }

    enqueue_dump(NAND_BANK_1, POWER_AFTER_DONE_KEEP, true, 0U);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUMP);

    run_until_duty(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.usb.bytes_written == 3U * DUMP_MODE_PACKET_SIZE);
    assert((board_nand_is_powered(1U, &powered) == BOARD_OK) && (powered == 1U));
}

/* Dump-all on an empty bank is rejected (no data to output, mode_dump §3.4). */
static void dump_all_rejected_when_bank_empty(void) {
    SystemContext ctx;
    uint8_t reply[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_DUTY);
    assert(board_nand_erase_start(2U) == BOARD_OK); /* bank2 empty */

    enqueue_dump(NAND_BANK_2, POWER_AFTER_DONE_OFF, true, 0U);
    algorithm_process_events(&ctx);
    assert(transport_poll(&ctx, 0U) == BOARD_OK); /* flush the NAK */

    assert(ctx.state == STATE_DUTY);
    assert(board_comm_stub_last_tx(&message_id, &address_to, reply, sizeof(reply),
                                   &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(reply[2] == ACK_ERR_CONTENT);
}

/* CAN output (interface bit 3 = 1) is rejected by the parser: only USB output
 * is supported (mode_dump §4.2). */
static void can_output_interface_rejected(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    begin_test(&ctx, STATE_DUTY);

    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    payload[0] = 0x01U | 0x08U; /* NAND1 + interface = CAN */
    payload[1] = 0x01U;         /* packet count = 1 (u24) */
    payload[2] = 0x00U;
    payload[3] = 0x00U;
    board_comm_stub_inject_rx(KU_DUMP_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U); /* not accepted */
    assert(ctx.state == STATE_DUTY);
}

/* CMD_STATUS_REQ is answered with a status TS and keeps the mode. */
static void status_req_allowed_in_dump(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_DUMP);

    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    algorithm_process_events(&ctx);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(board_comm_stub_last_tx(&message_id, &address_to, NULL, 0U, &length));
    assert(message_id == TS_STATUS_MSG_ID);
    assert(ctx.state == STATE_DUMP);
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

    begin_test(&ctx, STATE_DUMP);

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
    assert(reply[2] == ACK_ERR_MODE);
    assert(ctx.state == STATE_DUMP);
}

/* KT (telemetry) messages are ignored in DUMP with no quittance. */
static void telemetry_ignored_in_dump(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUMP);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_ORBIT;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);

    assert(ctx.state == STATE_DUMP);
    assert(board_comm_stub_tx_count() == 0U);
}

/* mode_dump §15.2.2/3: a normal dump finish persists the last successfully
 * output packet counter of the selected bank to MRAM service data. */
static void dump_persists_last_dumped_packet(void) {
    SystemContext ctx;
    MramStoreServiceData service_data;

    begin_test(&ctx, STATE_DUTY);

    enqueue_dump(NAND_BANK_1, POWER_AFTER_DONE_OFF, false, 2U);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUMP);

    run_until_duty(&ctx);
    assert(ctx.state == STATE_DUTY);
    assert(ctx.dump.last_dumped_packet == 2U);

    memset(&service_data, 0, sizeof(service_data));
    assert(mram_store_load_service_data(&service_data) == BOARD_OK);
    assert(service_data.nand1_last_dumped_packet == 2U);
}

/* mode_dump §16.1.4: an early finish by CMD_DUTY persists the count of packets
 * already output to MRAM before returning to DUTY. */
static void command_finish_persists_last_dumped_packet(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreServiceData service_data;
    uint32_t partial;
    uint32_t guard;

    begin_test(&ctx, STATE_DUTY);

    enqueue_dump(NAND_BANK_1, POWER_AFTER_DONE_OFF, false, 10U);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUMP);

    for (guard = 0U; (guard < 5U) && (ctx.state == STATE_DUMP); ++guard) {
        algorithm_poll(&ctx);
        algorithm_process_events(&ctx);
    }
    assert(ctx.state == STATE_DUMP);
    partial = ctx.dump.last_dumped_packet;
    assert(partial > 0U);
    assert(partial < 10U);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUTY;
    event.msg_id = 0x0003U;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_DUTY);

    memset(&service_data, 0, sizeof(service_data));
    assert(mram_store_load_service_data(&service_data) == BOARD_OK);
    assert(service_data.nand1_last_dumped_packet == partial);
}

/* A masked alarm during DUMP forces ALARM with PED and both NAND banks off. */
static void masked_alarm_in_dump_enters_alarm(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t powered1 = 1U;
    uint8_t powered2 = 1U;

    begin_test(&ctx, STATE_DUMP);
    ctx.dump.bank = NAND_BANK_1;
    ctx.dump.stage = DUMP_STAGE_READ;
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
}

int main(void) {
    dump_ack_reports_packet_count();
    fixed_count_dump_completes_to_duty();
    dump_all_outputs_committed_packets();
    dump_all_rejected_when_bank_empty();
    can_output_interface_rejected();
    status_req_allowed_in_dump();
    forbidden_command_rejected_with_err_mode();
    telemetry_ignored_in_dump();
    dump_persists_last_dumped_packet();
    command_finish_persists_last_dumped_packet();
    masked_alarm_in_dump_enters_alarm();

    return 0;
}
