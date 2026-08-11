#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "alarm_monitor.h"
#include "board_stub.h"
#include "event_queue.h"
#include "state.h"

/* Thresholds are stored in ctx in wire units (temp deci-degC, voltage mV,
 * current mA); the monitor converts to Board_API native units. These are wide
 * enough that the default in-range stub readings pass. */
static void set_wide_thresholds(SystemContext *ctx) {
    ctx->pu_temp_min = -400;
    ctx->pu_temp_max = 850;
    ctx->ped_temp_min = -400;
    ctx->ped_temp_max = 850;
    ctx->pu_voltage_min = 0U;
    ctx->pu_voltage_max = 60000U;
    ctx->ped_voltage_min = 0U;
    ctx->ped_voltage_max = 60000U;
    ctx->pu_current_min = 0U;
    ctx->pu_current_max = 60000U;
    ctx->ped_current_min = 0U;
    ctx->ped_current_max = 60000U;
    ctx->alarm_mask = ALARM_ALL_MASK;
}

static void reset_reads(void) {
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PU, 25000, true);
    board_stub_set_digital_temp(BOARD_TEMP_SENSOR_PED, 25000, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PU, 3300U, 0, true);
    board_stub_set_power_monitor(BOARD_POWER_MONITOR_PED, 3300U, 0, true);
    board_stub_set_ped_powered(true);
}

static void begin(SystemContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
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

int main(void) {
    alarm_raise_maskable_and_nonmaskable();
    in_range_no_alarm();
    pu_temp_over_max_alarms();
    invalid_read_is_skipped();
    pu_power_out_of_range_alarms();
    ped_ps_falling_edge_alarms();
    cadence_is_20s();
    shutdown_mode_not_monitored();

    return 0;
}
