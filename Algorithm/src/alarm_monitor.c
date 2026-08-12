#include "alarm_monitor.h"

#include <stdbool.h>

#include "alarm.h"
#include "board_api.h"
#include "event_queue.h"

#define ALARM_MONITOR_PERIOD_MS (20000U)

/*
 * Wire-unit -> Board_API native-unit conversion factors.
 *   temperature threshold assumed deci-degC, Board_API is milli-degC;
 *   current threshold assumed mA, Board_API is uA;
 *   voltage threshold assumed mV, matching Board_API (no conversion).
 */
#define ALARM_MONITOR_TEMP_DECI_TO_MILLI (100)
#define ALARM_MONITOR_CURR_MA_TO_UA      (1000)

void alarm_set(SystemContext *ctx, uint32_t bit) {
    if (ctx == NULL) {
        return;
    }

    ctx->alarm_status |= bit;
    ctx->masked_alarm = ctx->alarm_status & ctx->alarm_mask;
}

void alarm_raise(SystemContext *ctx, uint32_t bit) {
    uint32_t previous;

    if (ctx == NULL) {
        return;
    }

    previous = ctx->masked_alarm;
    ctx->alarm_status |= bit;
    ctx->masked_alarm = ctx->alarm_status & ctx->alarm_mask;

    if ((previous == 0U) && (ctx->masked_alarm != 0U)) {
        (void)system_event_queue_push_back_type(EVENT_MASKED_ALARM_SET);
    }
}

static bool alarm_monitor_mode(SystemState state) {
    return (state == STATE_DUTY) || (state == STATE_ERASE) ||
        (state == STATE_TEST) || (state == STATE_OBSERVE) ||
        (state == STATE_DUMP) || (state == STATE_ALARM);
}

static void check_digital_temp(SystemContext *ctx, BoardTempSensorId sensor,
                               int16_t min_deci, int16_t max_deci, uint32_t bit) {
    BoardDigitalTempSample sample;
    int32_t min_milli;
    int32_t max_milli;

    if (board_read_digital_temp(sensor, &sample) != BOARD_OK) {
        return;
    }
    if ((sample.ready == 0U) || (sample.range_valid == 0U)) {
        return;
    }

    min_milli = (int32_t)min_deci * ALARM_MONITOR_TEMP_DECI_TO_MILLI;
    max_milli = (int32_t)max_deci * ALARM_MONITOR_TEMP_DECI_TO_MILLI;

    if ((sample.temperature_milli_c < min_milli) ||
        (sample.temperature_milli_c > max_milli)) {
        alarm_raise(ctx, bit);
    }
}

static void check_power(SystemContext *ctx, BoardPowerMonitorId monitor,
                        uint16_t v_min, uint16_t v_max, uint32_t v_bit,
                        uint16_t c_min_ma, uint16_t c_max_ma, uint32_t c_bit) {
    BoardPowerSample sample;
    int32_t c_min_ua;
    int32_t c_max_ua;

    if (board_read_power_monitor(monitor, &sample) != BOARD_OK) {
        return;
    }
    if (sample.ready == 0U) {
        return;
    }

    if ((sample.bus_voltage_mv < v_min) || (sample.bus_voltage_mv > v_max)) {
        alarm_raise(ctx, v_bit);
    }

    c_min_ua = (int32_t)c_min_ma * ALARM_MONITOR_CURR_MA_TO_UA;
    c_max_ua = (int32_t)c_max_ma * ALARM_MONITOR_CURR_MA_TO_UA;

    if ((sample.current_ua < c_min_ua) || (sample.current_ua > c_max_ua)) {
        alarm_raise(ctx, c_bit);
    }
}

static void check_ped_power_state(SystemContext *ctx) {
    uint8_t is_powered = 0U;

    if (!ctx->ped.is_powered) {
        return;
    }
    if (board_ped_is_powered(&is_powered) != BOARD_OK) {
        return;
    }
    if (is_powered == 0U) {
        alarm_raise(ctx, ALARM_PED_PS);
    }
}

static void check_nand_power_state(SystemContext *ctx,
                                   const NandRuntimeState *nand) {
    uint8_t is_powered = 0U;

    if (!nand->is_powered) {
        return;
    }
    if ((nand->bank != NAND_BANK_1) && (nand->bank != NAND_BANK_2)) {
        return;
    }
    if (board_nand_is_powered((uint8_t)nand->bank, &is_powered) != BOARD_OK) {
        return;
    }
    if (is_powered == 0U) {
        alarm_raise(ctx, ALARM_NAND_PS);
    }
}

void alarm_monitor_poll(SystemContext *ctx, uint32_t now_ms) {
    if (ctx == NULL) {
        return;
    }
    if (!alarm_monitor_mode(ctx->state)) {
        return;
    }
    if ((uint32_t)(now_ms - ctx->alarm_monitor_last_ms) < ALARM_MONITOR_PERIOD_MS) {
        return;
    }
    ctx->alarm_monitor_last_ms = now_ms;

    check_digital_temp(ctx, BOARD_TEMP_SENSOR_PU,
                       ctx->pu_temp_min, ctx->pu_temp_max, ALARM_PU_TEMP);
    check_digital_temp(ctx, BOARD_TEMP_SENSOR_PED,
                       ctx->ped_temp_min, ctx->ped_temp_max, ALARM_PED_TEMP);

    check_power(ctx, BOARD_POWER_MONITOR_PU,
                ctx->pu_voltage_min, ctx->pu_voltage_max, ALARM_PU_VOLT,
                ctx->pu_current_min, ctx->pu_current_max, ALARM_PU_CURR);
    check_power(ctx, BOARD_POWER_MONITOR_PED,
                ctx->ped_voltage_min, ctx->ped_voltage_max, ALARM_PED_VOLT,
                ctx->ped_current_min, ctx->ped_current_max, ALARM_PED_CURR);

    check_ped_power_state(ctx);
    check_nand_power_state(ctx, &ctx->nand1);
    check_nand_power_state(ctx, &ctx->nand2);
}
