#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "alarm_monitor.h"
#include "algorithm.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "nand_map.h"
#include "state.h"
#include "transport.h"

/* Alarm policy from "Порядок работы с интерфейсами v2": the alarm status starts
 * clear on every boot, MRAM faults at boot raise ALARM_MRAM, PED faults are
 * collected in every mode, and a failed command enters ALARM only when the
 * raised bit is enabled in the mask. */

static void begin_test(SystemContext *ctx, SystemState state, uint32_t alarm_mask) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = state;
    ctx->previous_state = state;
    ctx->alarm_mask = alarm_sanitize_mask(alarm_mask);
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static void store_mram(uint16_t stored_alarm_status) {
    MramStoreConfig config;
    MramStoreServiceData service;

    memset(&config, 0, sizeof(config));
    config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    assert(mram_store_save_config(&config) == BOARD_OK);

    memset(&service, 0, sizeof(service));
    service.alarm_status = stored_alarm_status;
    service.nand1_full = 1U;
    service.nand1_packet_count = 123U;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static SystemEvent make_event(EventType type) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    return event;
}

static void boot_starts_without_alarms(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_INIT, 0U);
    store_mram(0xFFFFU);
    ctx.alarm_status = ALARM_PED_PS;
    ctx.masked_alarm = ALARM_PED_PS;

    event = make_event(EVENT_BOOT);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_INIT);
    assert(ctx.alarm_status == 0U);
    assert(ctx.masked_alarm == 0U);
    assert(ctx.alarm_mask == alarm_sanitize_mask(ALARM_ALL_MASK));
    assert(ctx.nand1.is_full);

    event = make_event(EVENT_INIT_DONE);
    (void)handle_event(&ctx, &event);
    assert(ctx.state == STATE_DUTY);
}

static void boot_with_both_mram_copies_invalid_raises_mram(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_INIT, 0U);
    store_mram(0U);
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    event = make_event(EVENT_BOOT);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
}

static void can_init_failure_does_not_claim_mram(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_INIT, ALARM_ALL_MASK);

    event = make_event(EVENT_INIT_FAIL);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) == 0U);
}

static void ped_fault_in_duty_enters_alarm(void) {
    SystemContext ctx;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK);
    ctx.ped.is_powered = true;
    board_stub_set_ped_faults(BOARD_PED_FAULT_POWER | BOARD_PED_FAULT_READY);

    algorithm_collect_hw_events(&ctx);

    assert(ctx.alarm_status == (ALARM_PED_PS | ALARM_PED_DIR));
    assert(!ctx.ped.is_powered);
    assert(system_event_queue_get_count() == 1U);

    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ALARM);
}

static void masked_off_ped_fault_in_duty_keeps_duty(void) {
    SystemContext ctx;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK & ~ALARM_PED_ST);
    board_stub_set_ped_faults(BOARD_PED_FAULT_STATUS);

    algorithm_collect_hw_events(&ctx);
    algorithm_process_events(&ctx);

    assert(ctx.alarm_status == ALARM_PED_ST);
    assert(ctx.masked_alarm == 0U);
    assert(ctx.state == STATE_DUTY);
}

static void set_cfg_mram_failure(uint32_t alarm_mask, SystemState expected_state) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUTY, alarm_mask);
    store_mram(0U);
    board_stub_set_mram_write_fail(true);

    event = make_event(EVENT_CMD_SET_CFG);
    (void)handle_event(&ctx, &event);

    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert(ctx.state == expected_state);
}

static void service_data_load_failure_does_not_overwrite(void) {
    SystemContext ctx;
    MramStoreServiceData service;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK);
    store_mram(0U);
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    ctx.erase.bank = NAND_BANK_1;
    assert(action_update_erase_service_data(&ctx) == ACTION_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);

    board_stub_set_mram_crc_valid(1U, true);
    board_stub_set_mram_crc_valid(2U, true);
    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.nand1_packet_count == 123U);
    assert(service.nand1_erase_count == 0U);
}

/* Blank MRAM (both copies invalid): boot enters ALARM with ALARM_MRAM active,
 * RESET_ALARM does not leave ALARM while MRAM is still blank, KU 8 writes the
 * configuration and zeroed service data with both banks marked "needs erase",
 * clears ALARM_MRAM and the FSM returns to DUTY. */
static void blank_mram_waits_for_set_cfg(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreConfig config;
    MramStoreServiceData service;

    begin_test(&ctx, STATE_INIT, 0U);
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    event = make_event(EVENT_BOOT);
    (void)handle_event(&ctx, &event);
    assert(ctx.state == STATE_ALARM);
    assert((ctx.masked_alarm & ALARM_MRAM) != 0U);

    event = make_event(EVENT_CMD_RESET_ALARM);
    (void)handle_event(&ctx, &event);
    assert(ctx.state == STATE_ALARM);
    assert((ctx.masked_alarm & ALARM_MRAM) != 0U);

    alarm_monitor_poll(&ctx, 20000U);
    alarm_monitor_poll(&ctx, 40000U);
    assert(ctx.alarm_status == ALARM_MRAM);

    event = make_event(EVENT_CMD_SET_CFG);
    event.command.set_config.mcu_pu_temp_min = -400;
    event.command.set_config.mcu_pu_temp_max = 850;
    event.command.set_config.det_temp_min = -400;
    event.command.set_config.det_temp_max = 850;
    event.command.set_config.pu_temp_min = -400;
    event.command.set_config.pu_temp_max = 850;
    event.command.set_config.pu_voltage_max = 6000U;
    event.command.set_config.pu_current_max = 1000U;
    event.command.set_config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.thresholds_loaded);
    alarm_monitor_poll(&ctx, 60000U);
    assert(ctx.alarm_status == 0U);
    assert(ctx.alarm_status == 0U);
    assert(ctx.pu_temp_max == 850);
    assert(ctx.nand1.is_full && ctx.nand2.is_full);

    memset(&config, 0, sizeof(config));
    assert(mram_store_load_config(&config) == BOARD_OK);
    assert(config.pu_temp_max == 850);
    memset(&service, 0xFF, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.nand1_full == 1U && service.nand2_full == 1U);
    assert(service.nand1_packet_count == 0U && service.nand2_packet_count == 0U);
    assert(service.observe_session_id == 0U);
}

/* TS "Telemetry" with unreadable MRAM: the frame still goes out with the
 * thresholds and mask the firmware works with, and ALARM_MRAM is raised. */
static void telemetry_with_unreadable_mram_raises_mram(void) {
    SystemContext ctx;
    uint8_t got[109];
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK);
    ctx.pu_temp_max = 850;
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    assert(action_send_telem(&ctx) == ACTION_OK);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got), &length));
    assert(length == sizeof(got));
    assert(((uint16_t)got[42] | ((uint16_t)got[43] << 8)) == 850U);

    assert((ctx.masked_alarm & ALARM_MRAM) != 0U);
    assert(system_event_queue_get_count() == 1U);
}

/* A valid block map whose second copy cannot be repaired is an MRAM fault. */
static void block_map_repair_failure_is_mram_fault(void) {
    SystemContext ctx;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK);
    assert(nand_map_rebuild(1U) == BOARD_OK);
    board_stub_set_mram_block_map_valid(2U, 1U, false);
    board_stub_set_mram_write_fail(true);

    assert(nand_map_load(1U) != BOARD_OK);
    assert(nand_map_mram_failed());
}

/* T-07: a bank switched on without power-good confirmation raises the
 * non-maskable ALARM_NAND_PS and the command leads to ALARM, even with bit 11
 * cleared in the requested mask. A confirmed bank raises nothing. */
static void unconfirmed_nand_power_enters_alarm(uint32_t mask) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUTY, mask);
    store_mram(0U);
    board_stub_set_nand_powered(1U, false);

    event = make_event(EVENT_CMD_ERASE);
    event.command.erase.bank = NAND_BANK_1;
    (void)handle_event(&ctx, &event);

    assert((ctx.alarm_status & ALARM_NAND_PS) != 0U);
    assert(ctx.state == STATE_ALARM);
}

static void confirmed_nand_power_raises_nothing(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_test(&ctx, STATE_DUTY, ALARM_ALL_MASK);
    store_mram(0U);

    event = make_event(EVENT_CMD_ERASE);
    event.command.erase.bank = NAND_BANK_1;
    (void)handle_event(&ctx, &event);

    assert(ctx.alarm_status == 0U);
    assert(ctx.state == STATE_ERASE);
}

int main(void) {
    unconfirmed_nand_power_enters_alarm(ALARM_ALL_MASK);
    unconfirmed_nand_power_enters_alarm(ALARM_ALL_MASK & ~ALARM_NAND_PS);
    confirmed_nand_power_raises_nothing();
    boot_starts_without_alarms();
    boot_with_both_mram_copies_invalid_raises_mram();
    can_init_failure_does_not_claim_mram();
    ped_fault_in_duty_enters_alarm();
    masked_off_ped_fault_in_duty_keeps_duty();
    set_cfg_mram_failure(ALARM_ALL_MASK, STATE_ALARM);
    set_cfg_mram_failure(ALARM_ALL_MASK & ~ALARM_MRAM, STATE_DUTY);
    service_data_load_failure_does_not_overwrite();
    blank_mram_waits_for_set_cfg();
    telemetry_with_unreadable_mram_raises_mram();
    block_map_repair_failure_is_mram_fault();

    return 0;
}
