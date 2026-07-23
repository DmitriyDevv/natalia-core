/*
 * TC1047 analog temperature sensor bring-up test (ADC + DMA).
 *
 * board_temp_start() kicks a DMA-driven ADC conversion; board_read_temp()
 * returns BOARD_ERR_BUSY / BOARD_ERR_NOT_READY until it completes, then OK with
 * the sample (and powers the ADC back down). This test does one measurement per
 * second, paced by a blocking delay (no RTC dependency), and logs the result.
 *
 * Requires NATALIA_ENABLE_ADC_DRIVER=ON and NATALIA_ENABLE_TC1047_DRIVER=ON.
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "status.h"
#include "timebase.h"

#define TC1047_TEST_PERIOD_MS 1000U
#define TC1047_TEST_POLL_TIMEOUT_MS 100U

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_temperature(int32_t milli_c) {
    int32_t whole = milli_c / 1000;
    int32_t frac = milli_c % 1000;

    if (frac < 0) {
        frac = -frac;
    }

    debug_log_write("temp_c=");
    if ((milli_c < 0) && (whole == 0)) {
        debug_log_write("-");
    }
    {
        uint32_t whole_mag = (whole < 0) ? (uint32_t)(-whole) : (uint32_t)whole;
        if (whole < 0) {
            debug_log_write("-");
        }
        debug_log_write_u32_inline(whole_mag);
    }
    debug_log_write(".");
    if (frac < 100) {
        debug_log_write("0");
    }
    if (frac < 10) {
        debug_log_write("0");
    }
    debug_log_write_u32_inline((uint32_t)frac);
    debug_log_write("\r\n");
}

static BoardStatus read_one_sample(BoardTempSample *sample) {
    BoardStatus status;
    uint32_t t0;

    status = board_temp_start();
    if (status != BOARD_OK) {
        return status;
    }

    t0 = timebase_millis();
    do {
        status = board_read_temp(sample);
    } while (((status == BOARD_ERR_BUSY) || (status == BOARD_ERR_NOT_READY)) &&
             ((timebase_millis() - t0) < TC1047_TEST_POLL_TIMEOUT_MS));

    return status;
}

int main(void) {
    BoardStatus status;
    uint32_t index = 0U;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\ntc1047 temp test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    status = board_temp_init();
    log_kv("board_temp_init", (uint32_t)status);
    if (status != BOARD_OK) {
        while (1) {
            __asm volatile("nop");
        }
    }

    while (1) {
        BoardTempSample sample;

        status = read_one_sample(&sample);

        debug_log_write("\r\n");
        log_kv("index", index);
        log_kv("read_status", (uint32_t)status);

        if (status == BOARD_OK) {
            log_temperature(sample.temperature_milli_c);
            log_kv("mv", sample.millivolts);
            log_kv("vdda_mv", sample.vdda_mv);
            log_kv("raw", sample.raw);
            log_kv("vrefint_raw", sample.vrefint_raw);
            log_kv("range_valid", sample.range_valid);
        }

        ++index;
        timebase_delay_ms_blocking(TC1047_TEST_PERIOD_MS);
    }
}
