/*
 * Стенд аналоговых температур (аварии MC_Temp / BD_Temp).
 *
 * Сборка: -DNATALIA_FIRMWARE_MAIN=tests/firmware/analog_temp_test.c
 *         -DNATALIA_ENABLE_ADC_DRIVER=ON -DNATALIA_ENABLE_TC1047_DRIVER=ON
 *         -DNATALIA_LOG_BACKEND=CAN
 *
 * Через 5 с после старта раз в 2 с вызывает board_read_analog_temps() — тот же
 * путь, что использует монитор аварий и телеметрия — и печатает:
 *   - сырой код встроенного датчика МК (ADC1_IN17) и VDDA по VREFINT;
 *   - калибровки TS_CAL1 (30 °C, 0x1FFF75A8) и TS_CAL2 (0x1FFF75CA);
 *   - температуру МК по прошивке (TS_CAL2 = 110 °C, RM0351) и, для сравнения,
 *     пересчёт для TS_CAL2 = 130 °C (так в части даташитов L4);
 *   - температуру БД с TC1047 (ADC12_IN5) и флаги достоверности.
 *
 * Как читать результат: разница TS_CAL2 - TS_CAL1 около 273 кодов говорит о
 * 110 °C, около 341 — о 130 °C. Температура МК на столе должна быть на
 * несколько градусов выше комнатной.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "natalia_build_info.h"
#include "timebase.h"

#define AT_START_DELAY_MS 5000UL
#define AT_PERIOD_MS      2000UL
#define AT_CAL1_C         30
#define AT_CAL2_ALT_C     130
#define AT_CAL_VDDA_MV    3000

static void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void log_line(const char* format, ...) {
    char text[200];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    debug_log_write(text);
}

static void milli_to_text(int32_t milli_c, char* text, size_t size) {
    int32_t magnitude = (milli_c < 0) ? -milli_c : milli_c;

    (void)snprintf(text, size, "%s%ld.%01ld", (milli_c < 0) ? "-" : "",
                   (long)(magnitude / 1000), (long)((magnitude % 1000) / 100));
}

static int32_t mcu_temp_alt_milli_c(const BoardAnalogTemps* temps) {
    int32_t scaled;

    if ((temps->ts_cal2 <= temps->ts_cal1) || (temps->vdda_mv == 0U)) {
        return 0;
    }

    scaled = (int32_t)(((uint32_t)temps->mcu_raw * temps->vdda_mv) / AT_CAL_VDDA_MV);

    return (int32_t)(((int64_t)(AT_CAL2_ALT_C - AT_CAL1_C) * 1000 *
                      (scaled - (int32_t)temps->ts_cal1)) /
                     ((int32_t)temps->ts_cal2 - (int32_t)temps->ts_cal1)) +
           (AT_CAL1_C * 1000);
}

int main(void) {
    BoardAnalogTemps temps;
    BoardStatus status;
    uint32_t index = 0U;
    char mcu_text[16];
    char alt_text[16];
    char bd_text[16];

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(AT_START_DELAY_MS);
    log_line("\r\n===== ANALOG TEMP TEST (MC_Temp / BD_Temp) =====\r\n");
    log_line("build %s\r\n", NATALIA_BUILD_TIME);

    status = board_init_hardware();
    log_line("board_init_hardware=%d\r\n", (int)status);

    for (;;) {
        status = board_read_analog_temps(&temps);
        ++index;

        if (status != BOARD_OK) {
            log_line("#%lu board_read_analog_temps=%d\r\n", (unsigned long)index, (int)status);
        } else {
            milli_to_text(temps.mcu_milli_c, mcu_text, sizeof(mcu_text));
            milli_to_text(mcu_temp_alt_milli_c(&temps), alt_text, sizeof(alt_text));
            milli_to_text(temps.bd_milli_c, bd_text, sizeof(bd_text));

            log_line("#%lu mcu_raw=%u vdda=%lu cal1=%u cal2=%u d=%d\r\n",
                     (unsigned long)index, (unsigned)temps.mcu_raw,
                     (unsigned long)temps.vdda_mv, (unsigned)temps.ts_cal1,
                     (unsigned)temps.ts_cal2,
                     (int)temps.ts_cal2 - (int)temps.ts_cal1);
            log_line("   MC=%s C (cal2=110) / %s C (cal2=130) valid=%u  BD=%s C valid=%u\r\n",
                     mcu_text, alt_text, (unsigned)temps.mcu_valid,
                     bd_text, (unsigned)temps.bd_valid);
        }

        timebase_delay_ms_blocking(AT_PERIOD_MS);
    }
}
