/*
 * NAND ID probe. For each bank, drives the power switch each way and tries a
 * single READ_ID (one bounded QSPI transaction, ~0.5 s worst case, no nested
 * NAND retry loops, so it will not appear to hang). READ_ID needs no init.
 *
 * Expected healthy result: read_id=0, id=2C34 (Micron MT29F).
 *   read_id=2 (TIMEOUT) + id=0000 -> no response (unpowered / wrong polarity).
 *   read_id=3 (IO) + id=<other>   -> a response, but wrong bytes (comms issue).
 *
 * PSON is only a fault flag on this board, so it cannot confirm power; this test
 * confirms power+comms directly by whether the chip answers.
 */

#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "board_pins.h"
#include "clock.h"
#include "debug_log.h"
#include "gpio.h"
#include "nand_mt29f.h"
#include "qspi.h"
#include "status.h"
#include "timebase.h"

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_id(const NandMt29fId *id) {
    static const char hex[] = "0123456789ABCDEF";
    char text[9];
    size_t i;

    for (i = 0U; i < 4U; ++i) {
        text[i * 2U] = hex[(id->bytes[i] >> 4) & 0x0FU];
        text[(i * 2U) + 1U] = hex[id->bytes[i] & 0x0FU];
    }
    text[8] = '\0';

    debug_log_write("id=");
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void try_read_id(uint8_t bank_id, NandMt29fBank bank,
                        BoardPinId ps_pin, GpioLevel ps_level) {
    NandMt29fId id = {0U, 0U, {0U, 0U, 0U, 0U}};
    BoardStatus status;

    debug_log_write("\r\nbank ");
    debug_log_write_u32_inline((uint32_t)bank_id);
    debug_log_write((ps_level == GPIO_LEVEL_LOW) ? " ps=LOW\r\n" : " ps=HIGH\r\n");

    (void)gpio_write(ps_pin, ps_level);
    timebase_delay_ms_blocking(50U);

    status = nand_mt29f_select_bank(bank);
    log_kv("select_bank", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = nand_mt29f_read_id(&id);
    log_kv("read_id", (uint32_t)status);
    log_id(&id);
}

int main(void) {
    const GpioConfig ps_out = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_HIGH
    };

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand id probe start\r\n");

    (void)board_init_hardware();

    log_kv("qspi_init", (uint32_t)qspi_init());
    log_kv("qspi_dma_init", (uint32_t)qspi_dma_init());

    (void)gpio_configure(BOARD_PIN_PU_NAND1_PS, &ps_out);
    (void)gpio_configure(BOARD_PIN_PU_NAND2_PS, &ps_out);

    /* Bank 1: try both polarities, then park it off before touching bank 2. */
    try_read_id(1U, NAND_MT29F_BANK_1, BOARD_PIN_PU_NAND1_PS, GPIO_LEVEL_LOW);
    try_read_id(1U, NAND_MT29F_BANK_1, BOARD_PIN_PU_NAND1_PS, GPIO_LEVEL_HIGH);
    (void)gpio_write(BOARD_PIN_PU_NAND1_PS, GPIO_LEVEL_HIGH);
    timebase_delay_ms_blocking(50U);

    try_read_id(2U, NAND_MT29F_BANK_2, BOARD_PIN_PU_NAND2_PS, GPIO_LEVEL_LOW);
    try_read_id(2U, NAND_MT29F_BANK_2, BOARD_PIN_PU_NAND2_PS, GPIO_LEVEL_HIGH);
    (void)gpio_write(BOARD_PIN_PU_NAND2_PS, GPIO_LEVEL_HIGH);

    debug_log_write("\r\nnand id probe done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
