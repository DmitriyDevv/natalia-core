#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "status.h"
#include "timebase.h"

static void panic_loop(void) {
    while (1) {
        __asm volatile ("nop");
    }
}

static void wait_ok(BoardStatus status) {
    if (status != BOARD_OK) {
        panic_loop();
    }
}

int main(void) {
    uint32_t last_ms = 0U;
    uint32_t counter = 0U;

    wait_ok(clock_init());
    wait_ok(timebase_init());
    wait_ok(board_init_hardware());

    while (1) {
        uint8_t ready = 0U;

        if (timebase_elapsed(last_ms, 1000U)) {
            size_t written = 0U;
            char message[64];
            uint32_t value;
            uint32_t pos = 0U;

            last_ms = timebase_millis();

            wait_ok(board_usb_is_ready(&ready));

            if (ready != 0U) {
                const char prefix[] = "NATALIA USB CDC OK ";
                const char suffix[] = "\r\n";

                for (uint32_t i = 0U; i < sizeof(prefix) - 1U; ++i) {
                    message[pos++] = prefix[i];
                }

                value = counter++;

                if (value >= 1000U) {
                    message[pos++] = (char)('0' + ((value / 1000U) % 10U));
                }

                if (value >= 100U) {
                    message[pos++] = (char)('0' + ((value / 100U) % 10U));
                }

                if (value >= 10U) {
                    message[pos++] = (char)('0' + ((value / 10U) % 10U));
                }

                message[pos++] = (char)('0' + (value % 10U));

                for (uint32_t i = 0U; i < sizeof(suffix) - 1U; ++i) {
                    message[pos++] = suffix[i];
                }

                (void)board_usb_write(message, pos, &written);
            }
        }
    }
}
