/*
 * PA9 hardware-verification test. Drives USART1_TX (PA9) as a plain GPIO output,
 * 2 s HIGH then 2 s LOW, and reports the state over the CAN debug log.
 *
 * Purpose: with a multimeter, confirm PA9 physically swings 0 <-> 3.3 V and that
 * the same swing appears on the FTDI connector's RXD pin. This isolates a broken
 * PA9->FTDI-RXD wire / TX-RX swap from an MCU or FTDI fault.
 *
 * Requires the FTDI pin map (NATALIA_ENABLE_FTDI_DRIVER=ON) so BOARD_PIN_USART1_TX
 * resolves to PA9. board_init_hardware() is intentionally NOT called, so nothing
 * reconfigures PA9 away from this GPIO output.
 */

#include <stdint.h>

#include "board_pins.h"
#include "clock.h"
#include "debug_log.h"
#include "gpio.h"
#include "timebase.h"

int main(void) {
    const GpioConfig out_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    uint8_t level = 0U;

    clock_init();
    timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nPA9 toggle test start\r\n");

    (void)gpio_configure(BOARD_PIN_USART1_TX, &out_config);

    for (;;) {
        level ^= 1U;

        (void)gpio_write(BOARD_PIN_USART1_TX,
                         (level != 0U) ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);

        debug_log_write((level != 0U) ? "PA9=HIGH (expect ~3.3V)\r\n"
                                      : "PA9=LOW (expect ~0V)\r\n");

        timebase_delay_ms_blocking(2000U);
    }
}
