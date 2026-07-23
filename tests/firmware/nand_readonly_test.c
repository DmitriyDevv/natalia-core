/*
 * QSPI NAND read-only bring-up check. Non-destructive: no erase, no program.
 * Output goes over the CAN debug log (NATALIA_LOG_BACKEND=CAN).
 *
 * For each bank it:
 *   - powers the bank and brings up QSPI,
 *   - reads and validates the JEDEC ID (proves the QSPI link and the chip),
 *   - reads one existing page three ways (x1 / x2 / x4 IO lines) and checks that
 *     all three agree (validates the 1/2/4-line read paths against real data),
 *   - scans the first blocks for factory bad-block markers.
 *
 * The compiled read/program IO widths and DMA settings are reported so a run can
 * be matched to its build (NATALIA_NAND_CACHE_READ_MODE / _PROGRAM_LOAD_MODE,
 * NATALIA_QSPI_DMA_READ / _WRITE).
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

#define NAND_RO_COMPARE_SIZE 512U
#define NAND_RO_BAD_SCAN_BLOCKS 64U

static uint8_t buf_x1[NAND_RO_COMPARE_SIZE];
static uint8_t buf_x2[NAND_RO_COMPARE_SIZE];
static uint8_t buf_x4[NAND_RO_COMPARE_SIZE];

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_hex_bytes(const char *label, const uint8_t *data, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    char text[65];
    size_t pos = 0U;
    size_t i;

    if (size > 32U) {
        size = 32U;
    }

    for (i = 0U; i < size; ++i) {
        text[pos] = hex[(data[i] >> 4) & 0x0FU];
        ++pos;
        text[pos] = hex[data[i] & 0x0FU];
        ++pos;
    }
    text[pos] = '\0';

    debug_log_write(label);
    debug_log_write("=");
    debug_log_write(text);
    debug_log_write("\r\n");
}

static uint8_t buffers_equal(const uint8_t *a, const uint8_t *b, size_t size) {
    size_t i;

    for (i = 0U; i < size; ++i) {
        if (a[i] != b[i]) {
            return 0U;
        }
    }

    return 1U;
}

/*
 * Drive the bank power-switch enable directly and ignore the PSON feedback,
 * which does not assert reliably on this board. PS OFF level is HIGH (built with
 * NATALIA_NAND_PS_OFF_LEVEL=HIGH), so ON = LOW.
 */
static BoardStatus nand_power_bank(uint8_t bank_id) {
    const GpioConfig ps_on = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    BoardPinId ps_pin;
    BoardStatus status;

    if (bank_id == 1U) {
        ps_pin = BOARD_PIN_PU_NAND1_PS;
    } else if (bank_id == 2U) {
        ps_pin = BOARD_PIN_PU_NAND2_PS;
    } else {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_configure(ps_pin, &ps_on);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_write(ps_pin, GPIO_LEVEL_LOW);
    timebase_delay_ms_blocking(100U);

    return status;
}

static void nand_unpower_bank(uint8_t bank_id) {
    BoardPinId ps_pin;

    if (bank_id == 1U) {
        ps_pin = BOARD_PIN_PU_NAND1_PS;
    } else if (bank_id == 2U) {
        ps_pin = BOARD_PIN_PU_NAND2_PS;
    } else {
        return;
    }

    (void)gpio_write(ps_pin, GPIO_LEVEL_HIGH);
    timebase_delay_ms_blocking(100U);
}

static void check_bank(uint8_t bank_id, NandMt29fBank bank) {
    NandMt29fId id;
    BoardStatus status;
    uint32_t block;
    uint32_t bad_count = 0U;

    debug_log_write("\r\n--- bank ");
    debug_log_write_u32_inline((uint32_t)bank_id);
    debug_log_write(" ---\r\n");

    status = nand_power_bank(bank_id);
    log_kv("power_drive", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = nand_mt29f_select_bank(bank);
    log_kv("select_bank", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = nand_mt29f_read_id(&id);
    log_kv("read_id", (uint32_t)status);
    log_hex_bytes("id", id.bytes, sizeof(id.bytes));
    if (status != BOARD_OK) {
        return;
    }

    status = nand_mt29f_init();
    log_kv("nand_init", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = nand_mt29f_debug_read_cache_modes(0U, 0U,
                                               buf_x1, buf_x2, buf_x4,
                                               NAND_RO_COMPARE_SIZE);
    log_kv("read_x124", (uint32_t)status);
    if (status == BOARD_OK) {
        log_kv("x1_eq_x2", buffers_equal(buf_x1, buf_x2, NAND_RO_COMPARE_SIZE));
        log_kv("x1_eq_x4", buffers_equal(buf_x1, buf_x4, NAND_RO_COMPARE_SIZE));
        log_hex_bytes("x1_head", buf_x1, 8U);
        log_hex_bytes("x2_head", buf_x2, 8U);
        log_hex_bytes("x4_head", buf_x4, 8U);
    }

    for (block = 0U; block < NAND_RO_BAD_SCAN_BLOCKS; ++block) {
        uint8_t is_bad = 0U;

        if (nand_mt29f_is_block_bad(block, &is_bad) != BOARD_OK) {
            break;
        }

        if (is_bad != 0U) {
            ++bad_count;
        }
    }

    log_kv("bad_blocks_first64", bad_count);
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand readonly test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    status = qspi_init();
    log_kv("qspi_init", (uint32_t)status);

    status = qspi_dma_init();
    log_kv("qspi_dma_init", (uint32_t)status);

    log_kv("cache_read_mode", nand_mt29f_get_cache_read_mode());
    log_kv("program_load_mode", nand_mt29f_get_program_load_mode());

    check_bank(1U, NAND_MT29F_BANK_1);
    nand_unpower_bank(1U);

    check_bank(2U, NAND_MT29F_BANK_2);
    nand_unpower_bank(2U);

    debug_log_write("\r\nnand readonly test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
