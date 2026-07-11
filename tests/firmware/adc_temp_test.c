#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "status.h"
#include "stm32l496xx.h"
#include "timebase.h"

volatile uint32_t g_temp_update_period_seconds = 1UL;

static uint32_t temp_rtc_ticks;
static uint8_t temp_measurement_pending;
static uint32_t temp_measurement_index;

static const char* status_to_string(BoardStatus status) {
    switch (status) {
    case BOARD_OK:
        return "BOARD_OK";

    case BOARD_ERR_INVALID_ARG:
        return "BOARD_ERR_INVALID_ARG";

    case BOARD_ERR_TIMEOUT:
        return "BOARD_ERR_TIMEOUT";

    case BOARD_ERR_IO:
        return "BOARD_ERR_IO";

    case BOARD_ERR_CRC:
        return "BOARD_ERR_CRC";

    case BOARD_ERR_BUSY:
        return "BOARD_ERR_BUSY";

    case BOARD_ERR_NOT_READY:
        return "BOARD_ERR_NOT_READY";

    case BOARD_ERR_UNSUPPORTED:
        return "BOARD_ERR_UNSUPPORTED";

    default:
        return "BOARD_ERR_UNKNOWN";
    }
}

static void write_i32_inline(int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        debug_log_write("-");
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    debug_log_write_u32_inline(magnitude);
}

static void write_temp_milli_c_inline(int32_t temperature_milli_c) {
    int32_t whole;
    int32_t frac;

    whole = temperature_milli_c / 1000;
    frac = temperature_milli_c % 1000;

    if (frac < 0) {
        frac = -frac;
    }

    write_i32_inline(whole);
    debug_log_write(".");

    if (frac < 100) {
        debug_log_write("0");
    }

    if (frac < 10) {
        debug_log_write("0");
    }

    debug_log_write_u32_inline((uint32_t)frac);
}

static void log_status_line(const char* prefix, BoardStatus status) {
    debug_log_write(prefix);
    debug_log_write(status_to_string(status));
    debug_log_write("\n");
}

static void log_temp_sample(const BoardTempSample* sample) {
    debug_log_write("temp_sample index=");
    debug_log_write_u32_inline(temp_measurement_index);

    debug_log_write(" temp_c=");
    write_temp_milli_c_inline(sample->temperature_milli_c);

    debug_log_write(" raw=");
    debug_log_write_u32_inline((uint32_t)sample->raw);

    debug_log_write(" vref_raw=");
    debug_log_write_u32_inline((uint32_t)sample->vrefint_raw);

    debug_log_write(" mv=");
    debug_log_write_u32_inline(sample->millivolts);

    debug_log_write(" vdda_mv=");
    debug_log_write_u32_inline(sample->vdda_mv);

    debug_log_write(" adc_seq=");
    debug_log_write_u32_inline(sample->adc_sequence);

    debug_log_write(" ready=");
    debug_log_write_u32_inline((uint32_t)sample->ready);

    debug_log_write(" range=");
    debug_log_write_u32_inline((uint32_t)sample->range_valid);

    debug_log_write("\n");
}

static uint32_t get_temp_period_seconds(void) {
    uint32_t period;

    period = g_temp_update_period_seconds;

    if (period == 0UL) {
        period = 1UL;
    }

    return period;
}

static void start_temp_measurement(void) {
    BoardStatus status;

    if (temp_measurement_pending != 0U) {
        return;
    }

    ++temp_measurement_index;

    status = board_temp_start();
    if (status == BOARD_OK) {
        temp_measurement_pending = 1U;
        return;
    }

    log_status_line("board_temp_start=", status);
}

static void poll_temp_measurement(void) {
    BoardTempSample sample;
    BoardStatus status;

    if (temp_measurement_pending == 0U) {
        return;
    }

    status = board_read_temp(&sample);
    if (status == BOARD_OK) {
        temp_measurement_pending = 0U;
        log_temp_sample(&sample);
        return;
    }

    if ((status == BOARD_ERR_BUSY) || (status == BOARD_ERR_NOT_READY)) {
        return;
    }

    temp_measurement_pending = 0U;
    log_status_line("board_read_temp=", status);
    (void)board_temp_stop();
}

static void process_rtc_temp_period(void) {
    uint32_t rtc_events;
    uint32_t period;
    BoardStatus status;

    status = board_rtc_take_1hz_events(&rtc_events);
    if (status != BOARD_OK) {
        log_status_line("board_rtc_take_1hz_events=", status);
        return;
    }

    if (rtc_events == 0U) {
        return;
    }

    period = get_temp_period_seconds();

    temp_rtc_ticks += rtc_events;

    if (temp_rtc_ticks >= period) {
        temp_rtc_ticks %= period;
        start_temp_measurement();
    }
}

int main(void) {
    BoardStatus status;

    status = clock_init();
    if (status != BOARD_OK) {
        return 1;
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        return 1;
    }

    status = debug_log_init();
    if (status != BOARD_OK) {
        return 1;
    }

    debug_log_write("\n");
    debug_log_write("adc temp rtc polling test start\n");

    status = board_init_hardware();
    log_status_line("board_init_hardware=", status);
    if (status != BOARD_OK) {
        while (1) {
            __WFI();
        }
    }

    status = board_temp_init();
    log_status_line("board_temp_init=", status);
    if (status != BOARD_OK) {
        while (1) {
            __WFI();
        }
    }

    temp_rtc_ticks = 0U;
    temp_measurement_pending = 0U;
    temp_measurement_index = 0U;

    debug_log_write("temp_period_seconds=");
    debug_log_write_u32_inline(g_temp_update_period_seconds);
    debug_log_write("\n");

    start_temp_measurement();

    while (1) {
        process_rtc_temp_period();
        poll_temp_measurement();
        __WFI();
    }
}
