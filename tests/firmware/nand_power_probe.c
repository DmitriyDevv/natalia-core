/*
 * NAND bank power probe. Determines the power-switch enable polarity by driving
 * PU_NANDx_PS both ways and reading PU_NANDx_PSON after a 50 ms settle. No QSPI,
 * no NAND commands, so it cannot hang on an unpowered chip.
 *
 * Per the interface document:
 *   FPF2101 protection switch: PS=LOW enables.
 *   FPF2006 protection switch: PS=HIGH enables.
 *   After enabling + 50 ms, PSON=HIGH means powered OK, PSON=LOW means the
 *   switch tripped / not powered.
 *
 * One bank is probed at a time and left OFF before the next (two banks must
 * never be powered simultaneously).
 */

#include <stdint.h>

#include "board_api.h"
#include "board_pins.h"
#include "clock.h"
#include "debug_log.h"
#include "gpio.h"
#include "status.h"
#include "timebase.h"

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void probe_bank(uint8_t bank_id, BoardPinId ps_pin, BoardPinId pson_pin) {
    const GpioConfig ps_out = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_HIGH
    };
    const GpioConfig pson_in = {
        .mode = GPIO_MODE_INPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    GpioLevel pson_low = GPIO_LEVEL_LOW;
    GpioLevel pson_high = GPIO_LEVEL_LOW;

    debug_log_write("\r\n-- bank ");
    debug_log_write_u32_inline((uint32_t)bank_id);
    debug_log_write(" --\r\n");

    (void)gpio_configure(ps_pin, &ps_out);
    (void)gpio_configure(pson_pin, &pson_in);

    (void)gpio_write(ps_pin, GPIO_LEVEL_LOW);
    timebase_delay_ms_blocking(50U);
    (void)gpio_read(pson_pin, &pson_low);
    log_kv("ps_LOW_pson", (uint32_t)pson_low);

    (void)gpio_write(ps_pin, GPIO_LEVEL_HIGH);
    timebase_delay_ms_blocking(50U);
    (void)gpio_read(pson_pin, &pson_high);
    log_kv("ps_HIGH_pson", (uint32_t)pson_high);

    if (pson_low == GPIO_LEVEL_HIGH) {
        debug_log_write("=> powered by PS=LOW (FPF2101)\r\n");
        (void)gpio_write(ps_pin, GPIO_LEVEL_HIGH);
    } else if (pson_high == GPIO_LEVEL_HIGH) {
        debug_log_write("=> powered by PS=HIGH (FPF2006)\r\n");
        (void)gpio_write(ps_pin, GPIO_LEVEL_LOW);
    } else {
        debug_log_write("=> NO POWER either polarity\r\n");
        (void)gpio_write(ps_pin, GPIO_LEVEL_HIGH);
    }

    timebase_delay_ms_blocking(50U);
}

int main(void) {
    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand power probe start\r\n");

    (void)board_init_hardware();

    probe_bank(1U, BOARD_PIN_PU_NAND1_PS, BOARD_PIN_PU_NAND1_PSON);
    probe_bank(2U, BOARD_PIN_PU_NAND2_PS, BOARD_PIN_PU_NAND2_PSON);

    debug_log_write("\r\nnand power probe done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
