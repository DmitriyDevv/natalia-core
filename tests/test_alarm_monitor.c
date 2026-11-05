#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "alarm_monitor.h"
#include "board_stub.h"
#include "event_queue.h"
#include "state.h"

/* Thresholds are stored in ctx in wire units (temp 0.01 degC, voltage mV,
 * current mA); the monitor converts to Board_API native units. These are wide
 * enough that the default in-range stub readings pass. */
static void set_wide_thresholds(SystemContext *ctx) {
    ctx->mcu_temp_min = -4000;
    ctx->mcu_temp_max = 8500;
    ctx->bd_temp_min = -4000;
    ctx->bd_temp_max = 8500;
    ctx->pu_temp_min = -4000;
    ctx->pu_temp_max = 8500;
    ctx->ped_temp_min = -4000;
    ctx->ped_temp_max = 8500;
    ctx->pu_voltage_min = 0U;
    ctx->pu_voltage_max = 60000U;
    ctx->ped_voltage_min = 0U;
    ctx->ped_voltage_max = 60000U;
    ctx->pu_current_min = 0U;
    ctx->pu_current_max = 60000U;
    ctx->ped_current_min = 0U;
    ctx->ped_current_max = 60000U;
    ctx->alarm_mask = ALARM_ALL_MASK;
    ctx->thresholds_loaded = true;
}

static void reset_reads(void) {
    board_stub_set_analog_temp(true, 25000, true);
    board_stub_set_analog_temp(false, 25000, true);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 25000, true);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PED, 25000, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PU, 3300U, 0, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PED, 3300U, 0, true);
    board_stub_set_ped_powered(true);
}

static void begin(SystemContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();
    reset_reads();
    set_wide_thresholds(ctx);
}

static void alarm_raise_maskable_and_nonmaskable(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    ctx.alarm_mask = alarm_sanitize_mask(0U); /* maskable off, non-maskable on */
    system_event_queue_init();

    alarm_raise(&ctx, ALARM_PU_TEMP);
    assert((ctx.alarm_status & ALARM_PU_TEMP) != 0U);
    assert(ctx.masked_alarm == 0U);
    assert(system_event_queue_get_count() == 0U);

    alarm_raise(&ctx, ALARM_NAND_PS);
    assert((ctx.masked_alarm & ALARM_NAND_PS) != 0U);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_MASKED_ALARM_SET);
}

static void in_range_no_alarm(void) {
    SystemContext ctx;

    begin(&ctx);
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == 0U);
    assert(ctx.masked_alarm == 0U);
    assert(system_event_queue_get_count() == 0U);
}

static void pu_temp_over_max_alarms(void) {
    SystemContext ctx;
    SystemEvent event;

    begin(&ctx);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 90000, true); /* 90 C > 85 C */
    alarm_monitor_poll(&ctx, 20000U);

    assert((ctx.alarm_status & ALARM_PU_TEMP) != 0U);
    assert((ctx.masked_alarm & ALARM_PU_TEMP) != 0U);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_MASKED_ALARM_SET);
}

static void mc_temp_over_max_alarms(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_analog_temp(true, 90000, true); /* MCU sensor 90 C > 85 C */
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == ALARM_MC_TEMP);
    assert(system_event_queue_get_count() == 1U);
}

static void bd_temp_under_min_alarms(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_analog_temp(false, -45000, true); /* TC1047 -45 C < -40 C */
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == ALARM_BD_TEMP);
    assert(system_event_queue_get_count() == 1U);
}

static void analog_invalid_read_is_skipped(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_analog_temp(true, 90000, false);
    board_stub_set_analog_temp(false, -45000, false);
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == 0U);
    assert(system_event_queue_get_count() == 0U);
}

static void masked_off_mc_temp_sets_status_only(void) {
    SystemContext ctx;

    begin(&ctx);
    ctx.alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK & ~ALARM_MC_TEMP);
    board_stub_set_analog_temp(true, 90000, true);
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == ALARM_MC_TEMP);
    assert(ctx.masked_alarm == 0U);
    assert(system_event_queue_get_count() == 0U);
}

static void ped_sensors_checked_only_when_ped_powered(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PED, 90000, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PED, 70000U, 0, true);
    alarm_monitor_poll(&ctx, 20000U);
    assert(ctx.alarm_status == 0U);

    ctx.ped.is_powered = true;
    alarm_monitor_poll(&ctx, 40000U);
    assert(ctx.alarm_status == (ALARM_PED_TEMP | ALARM_PED_VOLT));
}

static void invalid_read_is_skipped(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 90000, false); /* out of range but invalid */
    alarm_monitor_poll(&ctx, 20000U);

    assert((ctx.alarm_status & ALARM_PU_TEMP) == 0U);
    assert(system_event_queue_get_count() == 0U);
}

static void pu_power_out_of_range_alarms(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PU, 70000U, 70000000, true);
    alarm_monitor_poll(&ctx, 20000U);

    assert((ctx.alarm_status & ALARM_PU_VOLT) != 0U);
    assert((ctx.alarm_status & ALARM_PU_CURR) != 0U);
    assert(system_event_queue_get_count() == 1U); /* single 0->nonzero edge */
}

static void ped_ps_falling_edge_alarms(void) {
    SystemContext ctx;

    begin(&ctx);
    ctx.ped.is_powered = true;         /* expected on */
    board_stub_set_ped_powered(false); /* but reads off */
    alarm_monitor_poll(&ctx, 20000U);

    assert((ctx.alarm_status & ALARM_PED_PS) != 0U);
    assert(system_event_queue_get_count() == 1U);
}

static void cadence_is_20s(void) {
    SystemContext ctx;

    begin(&ctx);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 90000, true);

    alarm_monitor_poll(&ctx, 19999U); /* before 20 s: no read */
    assert(ctx.alarm_status == 0U);

    alarm_monitor_poll(&ctx, 20000U); /* at 20 s: runs */
    assert((ctx.alarm_status & ALARM_PU_TEMP) != 0U);
}

static void shutdown_mode_not_monitored(void) {
    SystemContext ctx;

    begin(&ctx);
    ctx.state = STATE_SHUTDOWN;
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 90000, true);
    alarm_monitor_poll(&ctx, 20000U);

    assert(ctx.alarm_status == 0U);
}

/* Until the thresholds are loaded (MRAM at boot or KU 8) they are all zero, so
 * every reading would be "out of range". The range checks are skipped; the
 * power-confirmation checks still run. */
static void thresholds_not_loaded_skips_range_checks(void) {
    SystemContext ctx;

    begin(&ctx);
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_ALARM;
    ctx.alarm_mask = alarm_sanitize_mask(ALARM_ALL_MASK);
    ctx.nand1.bank = NAND_BANK_1;
    ctx.nand1.is_powered = true;
    board_stub_set_nand_powered(1U, false);

    alarm_monitor_poll(&ctx, 20000U);

    assert((ctx.alarm_status & (ALARM_MC_TEMP | ALARM_PU_TEMP | ALARM_BD_TEMP |
                                ALARM_PU_VOLT | ALARM_PU_CURR)) == 0U);
    assert((ctx.alarm_status & ALARM_NAND_PS) != 0U);

    ctx.thresholds_loaded = true;
    alarm_monitor_poll(&ctx, 40000U);
    assert((ctx.alarm_status & ALARM_PU_TEMP) != 0U);
}

int main(void) {
    alarm_raise_maskable_and_nonmaskable();
    thresholds_not_loaded_skips_range_checks();
    in_range_no_alarm();
    pu_temp_over_max_alarms();
    mc_temp_over_max_alarms();
    bd_temp_under_min_alarms();
    analog_invalid_read_is_skipped();
    masked_off_mc_temp_sets_status_only();
    ped_sensors_checked_only_when_ped_powered();
    invalid_read_is_skipped();
    pu_power_out_of_range_alarms();
    ped_ps_falling_edge_alarms();
    cadence_is_20s();
    shutdown_mode_not_monitored();

    return 0;
}
