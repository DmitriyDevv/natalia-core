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

/* T-02: the ways out of ALARM (protocol §5.2.7). The masked alarm status is
 * cleared only by "Reset alarm status" or by masking the raised bits with KU 8;
 * non-maskable bits cannot be masked; ALARM answers status, telemetry and version
 * requests and rejects mode commands. */

#define TS_STATUS_MSG_ID    (0x0D00U)
#define TS_ACK_MSG_ID       (0x0D01U)
#define TS_TELEMETRY_MSG_ID (0x0D02U)
#define TS_VERSION_MSG_ID   (0xFFE1U)
#define KU_RESET_ALARM_ID   (0x0F0CU)
#define KU_SET_CFG_ID       (0x0F07U)
#define KU_ERASE_ID         (0x0F08U)
#define KU_TELEM_ID         (0x0F00U)
#define ACK_OK              (0x00U)
#define ACK_ERR_MODE        (0x07U)
#define MODE_ALARM          (6U)
#define MODE_DUTY           (1U)

static void begin_alarm(SystemContext *ctx, uint32_t alarm_status) {
    MramStoreConfig config;
    MramStoreServiceData service;

    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_ALARM;
    ctx->previous_state = STATE_DUTY;
    ctx->alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx->alarm_status = alarm_status;
    ctx->masked_alarm = alarm_status & ctx->alarm_mask;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->thresholds_loaded = true;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(&config, 0, sizeof(config));
    config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    assert(mram_store_save_config(&config) == BOARD_OK);
    memset(&service, 0, sizeof(service));
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static SystemEvent command(EventType type, uint16_t msg_id) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = msg_id;
    return event;
}

static SystemEvent set_cfg(uint16_t alarm_mask) {
    SystemEvent event = command(EVENT_CMD_SET_CFG, KU_SET_CFG_ID);

    event.command.set_config.alarm_mask = alarm_mask;
    event.command.set_config.mcu_pu_temp_min = -4000;
    event.command.set_config.mcu_pu_temp_max = 8500;
    return event;
}

static void flush_tx(SystemContext *ctx) {
    uint32_t i;

    for (i = 0U; i < 24U; ++i) {
        (void)transport_poll(ctx, 0U);
    }
}

static uint32_t count_tx(uint16_t wanted) {
    uint32_t i;
    uint32_t total = board_comm_stub_tx_count();
    uint32_t first = (total > 8U) ? (total - 8U) : 0U;
    uint32_t found = 0U;
    uint16_t message_id;

    for (i = first; i < total; ++i) {
        assert(board_comm_stub_tx_at(i, &message_id, NULL, NULL, 0U, NULL));
        if (message_id == wanted) {
            ++found;
        }
    }
    return found;
}

static bool last_of(uint16_t wanted, uint8_t *payload, uint16_t capacity) {
    uint32_t total = board_comm_stub_tx_count();
    uint32_t i;
    uint16_t message_id;

    for (i = total; i > 0U; --i) {
        assert(board_comm_stub_tx_at(i - 1U, &message_id, NULL, NULL, 0U, NULL));
        if (message_id == wanted) {
            assert(board_comm_stub_tx_at(i - 1U, &message_id, NULL, payload, capacity, NULL));
            return true;
        }
    }
    return false;
}

static void assert_last_ack(uint16_t command_id, uint8_t status) {
    uint8_t payload[6];

    assert(last_of(TS_ACK_MSG_ID, payload, sizeof(payload)));
    assert(payload[0] == (uint8_t)(command_id & 0xFFU));
    assert(payload[1] == (uint8_t)(command_id >> 8U));
    assert(payload[2] == status);
}

static void assert_last_status_mode(uint8_t mode) {
    uint8_t payload[6];

    assert(last_of(TS_STATUS_MSG_ID, payload, sizeof(payload)));
    assert(((payload[0] >> 3U) & 0x07U) == mode);
}

static void reset_alarm_returns_to_duty(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_RESET_ALARM, KU_RESET_ALARM_ID);

    begin_alarm(&ctx, ALARM_PU_TEMP | ALARM_NAND_PR);
    (void)handle_event(&ctx, &event);
    flush_tx(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.alarm_status == 0U);
    assert(ctx.masked_alarm == 0U);
    assert_last_ack(KU_RESET_ALARM_ID, ACK_OK);
    assert_last_status_mode(MODE_DUTY);
}

static void set_cfg_masking_the_bit_exits(void) {
    SystemContext ctx;
    SystemEvent event = set_cfg((uint16_t)(ALARM_ALL_MASK & ~ALARM_PU_TEMP));

    begin_alarm(&ctx, ALARM_PU_TEMP);
    (void)handle_event(&ctx, &event);
    flush_tx(&ctx);

    assert(ctx.state == STATE_DUTY);
    assert((ctx.alarm_status & ALARM_PU_TEMP) != 0U);
    assert(ctx.masked_alarm == 0U);
    assert_last_ack(KU_SET_CFG_ID, ACK_OK);
    assert_last_status_mode(MODE_DUTY);
}

static void set_cfg_not_masking_stays(void) {
    SystemContext ctx;
    SystemEvent event = set_cfg((uint16_t)(ALARM_ALL_MASK & ~ALARM_PED_TEMP));

    begin_alarm(&ctx, ALARM_PU_TEMP);
    (void)handle_event(&ctx, &event);
    flush_tx(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert(ctx.masked_alarm == ALARM_PU_TEMP);
    assert_last_ack(KU_SET_CFG_ID, ACK_OK);
}

static void set_cfg_cannot_mask_non_maskable(void) {
    SystemContext ctx;
    SystemEvent event = set_cfg(0U);

    begin_alarm(&ctx, ALARM_NAND_PR);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert(ctx.masked_alarm == ALARM_NAND_PR);
    assert((ctx.alarm_mask & ALARM_NON_MASKABLE_MASK) == ALARM_NON_MASKABLE_MASK);
}

static void masked_alarm_clear_event(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_MASKED_ALARM_CLEAR, 0U);

    begin_alarm(&ctx, ALARM_PU_TEMP);
    (void)handle_event(&ctx, &event);
    assert(ctx.state == STATE_ALARM);

    ctx.masked_alarm = 0U;
    (void)handle_event(&ctx, &event);
    assert(ctx.state == STATE_DUTY);
}

static void requests_are_answered(void) {
    SystemContext ctx;
    SystemEvent status = command(EVENT_CMD_STATUS_REQ, 0x0F01U);
    SystemEvent telem = command(EVENT_CMD_TELEM_REQ, KU_TELEM_ID);
    SystemEvent version = command(EVENT_CMD_VERSION_REQ, 0xFFE0U);

    begin_alarm(&ctx, ALARM_PU_TEMP);
    (void)handle_event(&ctx, &status);
    (void)handle_event(&ctx, &telem);
    (void)handle_event(&ctx, &version);
    flush_tx(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert(count_tx(TS_ACK_MSG_ID) == 3U);
    assert(count_tx(TS_STATUS_MSG_ID) == 1U);
    assert(count_tx(TS_TELEMETRY_MSG_ID) == 1U);
    assert(count_tx(TS_VERSION_MSG_ID) == 1U);
    assert_last_status_mode(MODE_ALARM);
}

static void mode_commands_are_rejected(void) {
    SystemContext ctx;
    SystemEvent erase = command(EVENT_CMD_ERASE, KU_ERASE_ID);

    begin_alarm(&ctx, ALARM_PU_TEMP);
    erase.command.erase.bank = NAND_BANK_1;
    (void)handle_event(&ctx, &erase);
    flush_tx(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert_last_ack(KU_ERASE_ID, ACK_ERR_MODE);
}

static void shutdown_from_alarm(void) {
    SystemContext ctx;
    SystemEvent event = command(EVENT_CMD_SHUTDOWN, 0x0F0BU);

    begin_alarm(&ctx, ALARM_NAND_PR);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_SHUTDOWN);
}

int main(void) {
    reset_alarm_returns_to_duty();
    set_cfg_masking_the_bit_exits();
    set_cfg_not_masking_stays();
    set_cfg_cannot_mask_non_maskable();
    masked_alarm_clear_event();
    requests_are_answered();
    mode_commands_are_rejected();
    shutdown_from_alarm();

    return 0;
}
