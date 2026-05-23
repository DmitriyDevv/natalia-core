#include "board_api.h"
#include "can1.h"
#include "clock.h"
#include "debug_log.h"
#include "instrument_time.h"
#include "state.h"
#include "status.h"
#include "timebase.h"
#include "transport.h"
#include "unican.h"

int main(void) {
    BoardStatus status;
    InstrumentTime current_time;
    uint32_t last_print_ms;

    SystemContext ctx = {
        .state = STATE_DUTY,
        .previous_state = STATE_INIT,
        .alarm_status = 0U,
        .alarm_mask = 0U,
        .masked_alarm = 0U
    };

    status = debug_log_init();
    if (status != BOARD_OK) {
        while (1) {}
    }

    status = clock_init();
    if (status != BOARD_OK) {
        debug_log_write_u32("clock_init error = ", (uint32_t)status);
        while (1) {}
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        debug_log_write_u32("timebase_init error = ", (uint32_t)status);
        while (1) {}
    }

    status = board_init_hardware();
    if (status != BOARD_OK) {
        debug_log_write_u32("board_init_hardware error = ", (uint32_t)status);
        while (1) {}
    }

    status = can1_init();
    if (status != BOARD_OK) {
        debug_log_write_u32("can1_init error = ", (uint32_t)status);
        while (1) {}
    }

    unican_init();

    last_print_ms = timebase_millis();

    while (1) {
        status = transport_poll(&ctx, timebase_millis());

        if (status != BOARD_OK) {
            debug_log_write_u32("transport_poll error = ",
                                (uint32_t)status);
        }

        if (timebase_elapsed(last_print_ms, 1000U)) {
            last_print_ms = timebase_millis();

            status = board_rtc_get_time(&current_time);

            if (status == BOARD_OK) {
                debug_log_write_u32("RTC seconds = ",
                                    current_time.seconds);
                debug_log_write_u32("RTC milliseconds = ",
                                    (uint32_t)current_time.milliseconds);
            } else {
                debug_log_write_u32("RTC read error = ",
                                    (uint32_t)status);
            }
        }
    }
}
