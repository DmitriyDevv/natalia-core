/*
 * QSPI NAND destructive write/verify test. Erases and reprograms the first
 * NAND_WR_TEST_BLOCKS blocks of each bank, then reads every page back and
 * compares it byte-for-byte against a position-keyed pattern.
 *
 * DESTRUCTIVE: every tested block is erased. Only blocks 0..NAND_WR_TEST_BLOCKS-1
 * of each bank are touched.
 *
 * The pattern is a hash of (bank, block, page, offset), so there is no repeating
 * fill: consecutive bytes differ, every page differs, and the two banks hold
 * different data. Verification recomputes the same hash.
 *
 * DMA vs polling and the read/write IO widths are compile-time choices
 * (NATALIA_QSPI_DMA_READ / _WRITE, NATALIA_NAND_CACHE_READ_MODE /
 * _PROGRAM_LOAD_MODE). The reported modes tie a run to its build.
 *
 * Output goes over the CAN debug log (NATALIA_LOG_BACKEND=CAN).
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

#define NAND_WR_TEST_BLOCKS 8U
#define NAND_WR_PAGE_SIZE NAND_MT29F_PAGE_SIZE
#define NAND_WR_PAGES_PER_BLOCK NAND_MT29F_PAGES_PER_BLOCK

static uint8_t write_buf[NAND_WR_PAGE_SIZE];
static uint8_t read_buf[NAND_WR_PAGE_SIZE];

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

/*
 * Position-keyed pseudo-random byte. Mixes all four coordinates so the fill
 * never repeats and the value depends on the exact byte location.
 */
static uint8_t pattern_byte(uint8_t bank, uint32_t block, uint32_t page, uint32_t offset) {
    uint32_t x = 0x9E3779B9UL;

    x ^= (uint32_t)bank * 0x85EBCA6BUL;
    x ^= (block + 1UL) * 0xC2B2AE35UL;
    x ^= (page + 1UL) * 0x27D4EB2FUL;
    x ^= (offset + 1UL) * 0x165667B1UL;

    x ^= x >> 15;
    x *= 0x2C1B3C6DUL;
    x ^= x >> 12;
    x *= 0x297A2D39UL;
    x ^= x >> 15;

    return (uint8_t)(x & 0xFFU);
}

static void fill_page(uint8_t bank, uint32_t block, uint32_t page) {
    uint32_t offset;

    for (offset = 0U; offset < NAND_WR_PAGE_SIZE; ++offset) {
        write_buf[offset] = pattern_byte(bank, block, page, offset);
    }
}

/*
 * Drive the bank power-switch enable directly and ignore the PSON feedback,
 * which does not assert reliably on this board. FPF2101 enables active-low,
 * so ON = LOW.
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

/*
 * Read one page back and compare against the pattern. Returns the offset of the
 * first mismatch, or NAND_WR_PAGE_SIZE if the whole page matches.
 */
static uint32_t verify_page(const uint8_t *expected, const uint8_t *actual) {
    uint32_t offset;

    for (offset = 0U; offset < NAND_WR_PAGE_SIZE; ++offset) {
        if (actual[offset] != expected[offset]) {
            return offset;
        }
    }

    return NAND_WR_PAGE_SIZE;
}

static void check_bank(uint8_t bank_id, NandMt29fBank bank) {
    NandMt29fId id;
    BoardStatus status;
    uint32_t block;
    uint32_t pages_ok = 0U;
    uint32_t pages_fail = 0U;
    uint32_t blocks_bad = 0U;
    uint8_t have_first_fail = 0U;

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
    if (status != BOARD_OK) {
        return;
    }
    if ((id.manufacturer_id != 0x2CU) || (id.device_id != 0x34U)) {
        log_kv("id_mismatch_mfr", id.manufacturer_id);
        log_kv("id_mismatch_dev", id.device_id);
        return;
    }

    status = nand_mt29f_init();
    log_kv("nand_init", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    for (block = 0U; block < NAND_WR_TEST_BLOCKS; ++block) {
        uint8_t is_bad = 0U;
        uint32_t page;

        if (nand_mt29f_is_block_bad(block, &is_bad) != BOARD_OK) {
            log_kv("bad_check_fail_block", block);
            break;
        }
        if (is_bad != 0U) {
            log_kv("skip_bad_block", block);
            ++blocks_bad;
            continue;
        }

        status = nand_mt29f_erase_block(block);
        if (status != BOARD_OK) {
            log_kv("erase_fail_block", block);
            log_kv("erase_status", (uint32_t)status);
            break;
        }

        for (page = 0U; page < NAND_WR_PAGES_PER_BLOCK; ++page) {
            uint32_t mismatch;

            fill_page(bank_id, block, page);

            status = nand_mt29f_program_page(block, page, write_buf, NAND_WR_PAGE_SIZE);
            if (status != BOARD_OK) {
                ++pages_fail;
                if (have_first_fail == 0U) {
                    have_first_fail = 1U;
                    log_kv("first_program_fail_block", block);
                    log_kv("first_program_fail_page", page);
                    log_kv("program_status", (uint32_t)status);
                }
                continue;
            }

            status = nand_mt29f_read_page(block, page, read_buf, NAND_WR_PAGE_SIZE);
            if (status != BOARD_OK) {
                ++pages_fail;
                if (have_first_fail == 0U) {
                    have_first_fail = 1U;
                    log_kv("first_read_fail_block", block);
                    log_kv("first_read_fail_page", page);
                    log_kv("read_status", (uint32_t)status);
                }
                continue;
            }

            mismatch = verify_page(write_buf, read_buf);
            if (mismatch != NAND_WR_PAGE_SIZE) {
                ++pages_fail;
                if (have_first_fail == 0U) {
                    have_first_fail = 1U;
                    log_kv("first_mismatch_block", block);
                    log_kv("first_mismatch_page", page);
                    log_kv("first_mismatch_offset", mismatch);
                    log_kv("first_mismatch_expected", write_buf[mismatch]);
                    log_kv("first_mismatch_actual", read_buf[mismatch]);
                }
            } else {
                ++pages_ok;
            }
        }
    }

    log_kv("blocks_bad", blocks_bad);
    log_kv("pages_ok", pages_ok);
    log_kv("pages_fail", pages_fail);
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand write test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    status = qspi_init();
    log_kv("qspi_init", (uint32_t)status);

    status = qspi_dma_init();
    log_kv("qspi_dma_init", (uint32_t)status);

    log_kv("test_blocks", NAND_WR_TEST_BLOCKS);
    log_kv("cache_read_mode", nand_mt29f_get_cache_read_mode());
    log_kv("program_load_mode", nand_mt29f_get_program_load_mode());
#if defined(QSPI_DMA_READ_ENABLED)
    log_kv("dma_read", QSPI_DMA_READ_ENABLED);
#endif
#if defined(QSPI_DMA_WRITE_ENABLED)
    log_kv("dma_write", QSPI_DMA_WRITE_ENABLED);
#endif

    check_bank(1U, NAND_MT29F_BANK_1);
    nand_unpower_bank(1U);

    check_bank(2U, NAND_MT29F_BANK_2);
    nand_unpower_bank(2U);

    debug_log_write("\r\nnand write test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
