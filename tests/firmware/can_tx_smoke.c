/*
 * Minimal CAN1 TX smoke test. Sends one recognizable standard frame twice a
 * second, nothing else. Used to prove the natalia -> Nucleo CAN path in
 * isolation (the Nucleo translator dumps every frame to LPUART1).
 *
 * IMPORTANT / assumption under test:
 * The natalia board has CAN1 transceiver control lines that can1_configure_hardware()
 * does NOT drive:
 *   PU_CAN1_SHDN = PE0  (shutdown)
 *   PU_CAN1_S    = PE1  (silent-mode select)
 * Most CAN transceivers need SHDN low = normal, S low = normal. This test drives
 * both low. If frames appear on the Nucleo, the missing transceiver enable is the
 * bug and belongs in can1_config.c. If not, flip these levels / revisit the part.
 */

#include <stdint.h>

#include "board_pins.h"
#include "can1.h"
#include "clock.h"
#include "gpio.h"
#include "timebase.h"

static void can1_transceiver_enable_normal(void) {
    const GpioConfig out_low = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    (void)gpio_configure(BOARD_PIN_PU_CAN1_SHDN, &out_low);
    (void)gpio_configure(BOARD_PIN_PU_CAN1_S, &out_low);

    (void)gpio_write(BOARD_PIN_PU_CAN1_SHDN, GPIO_LEVEL_LOW);
    (void)gpio_write(BOARD_PIN_PU_CAN1_S, GPIO_LEVEL_LOW);
}

int main(void) {
    CanFrame frame;
    uint32_t counter = 0U;

    clock_init();
    timebase_init();

    can1_transceiver_enable_normal();

    (void)can1_init();

    frame.identifier = 0x555U;
    frame.id_type = CAN_FRAME_STANDARD_ID;
    frame.frame_type = CAN_FRAME_DATA;
    frame.dlc = 8U;
    frame.data[0] = 0xDEU;
    frame.data[1] = 0xADU;
    frame.data[2] = 0xBEU;
    frame.data[3] = 0xEFU;
    frame.data[4] = 0xCAU;
    frame.data[5] = 0xFEU;
    frame.data[6] = 0x00U;
    frame.data[7] = 0x00U;

    for (;;) {
        frame.data[6] = (uint8_t)(counter >> 8);
        frame.data[7] = (uint8_t)(counter);

        (void)can1_send(&frame);

        ++counter;
        timebase_delay_ms_blocking(500U);
    }
}
