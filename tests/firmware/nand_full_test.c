/*
 * QSPI NAND full-bank destructive test with per-phase timing.
 *
 * Runs on ONE bank per build (NAND_WT_BANK_ID). The phases erase / write /
 * verify can each be turned off independently, so the same binary family can:
 *   - erase + write + verify a bank in one run, or
 *   - write in one run, power-cycle, then verify-only to prove NAND retention.
 *
 * DESTRUCTIVE for the selected bank: every good block in [0, NAND_WT_BLOCKS)
 * is erased and reprogrammed when those phases are enabled.
 *
 * Bad-block protection (always on): a raw block erase clears the whole block,
 * including the spare byte that holds the factory bad-block marker. So before
 * any phase this test scans every block for its marker and NEVER erases, writes
 * or verifies a block that is flagged bad. The markers are preserved.
 *
 * The fill is a hash of (bank, block, page, offset): no repeating pattern, every
 * byte position distinct, and each bank holds different data. Verify recomputes
 * the same hash. Each phase logs its duration in milliseconds.
 *
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..." or edit here):
 *   NAND_WT_BANK_ID    1 or 2
 *   NAND_WT_BLOCKS     number of blocks from 0 to test (default: whole bank)
 *   NAND_WT_DO_ERASE   0/1
 *   NAND_WT_DO_WRITE   0/1
 *   NAND_WT_DO_VERIFY  0/1
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

#ifndef NAND_WT_BANK_ID
#define NAND_WT_BANK_ID 1
#endif

#ifndef NAND_WT_BLOCKS
#define NAND_WT_BLOCKS NAND_MT29F_BLOCKS_PER_LUN
#endif

#ifndef NAND_WT_DO_ERASE
#define NAND_WT_DO_ERASE 1
#endif

#ifndef NAND_WT_DO_WRITE
#define NAND_WT_DO_WRITE 1
#endif

#ifndef NAND_WT_DO_VERIFY
#define NAND_WT_DO_VERIFY 1
#endif

#ifndef NAND_WT_PROGRESS_BLOCKS
#define NAND_WT_PROGRESS_BLOCKS 128
#endif

#if (NAND_WT_BANK_ID == 1)
#define NAND_WT_BANK NAND_MT29F_BANK_1
#define NAND_WT_PS_PIN BOARD_PIN_PU_NAND1_PS
#elif (NAND_WT_BANK_ID == 2)
#define NAND_WT_BANK NAND_MT29F_BANK_2
#define NAND_WT_PS_PIN BOARD_PIN_PU_NAND2_PS
#else
#error "NAND_WT_BANK_ID must be 1 or 2"
#endif

#if (NAND_WT_BLOCKS < 1) || (NAND_WT_BLOCKS > NAND_MT29F_BLOCKS_PER_LUN)
#error "NAND_WT_BLOCKS out of range"
#endif

#define NAND_WT_PAGE_SIZE NAND_MT29F_PAGE_SIZE
#define NAND_WT_PAGES_PER_BLOCK NAND_MT29F_PAGES_PER_BLOCK

static uint8_t bad_block[NAND_WT_BLOCKS];

#if (NAND_WT_DO_WRITE || NAND_WT_DO_VERIFY)
static uint8_t write_buf[NAND_WT_PAGE_SIZE];

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

static void fill_page(uint32_t block, uint32_t page) {
    uint32_t offset;

    for (offset = 0U; offset < NAND_WT_PAGE_SIZE; ++offset) {
        write_buf[offset] = pattern_byte((uint8_t)NAND_WT_BANK_ID, block, page, offset);
    }
}
#endif

#if NAND_WT_DO_VERIFY
static uint8_t read_buf[NAND_WT_PAGE_SIZE];
#endif

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

/*
 * Drive the bank power-switch enable directly and ignore the PSON feedback,
 * which does not assert reliably on this board. FPF2101 enables active-low,
 * so ON = LOW.
 */
static BoardStatus nand_power_bank(void) {
    const GpioConfig ps_on = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    BoardStatus status;

    status = gpio_configure(NAND_WT_PS_PIN, &ps_on);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_write(NAND_WT_PS_PIN, GPIO_LEVEL_LOW);
    timebase_delay_ms_blocking(100U);

    return status;
}

static void nand_unpower_bank(void) {
    (void)gpio_write(NAND_WT_PS_PIN, GPIO_LEVEL_HIGH);
    timebase_delay_ms_blocking(100U);
}

static uint32_t scan_bad_blocks(void) {
    uint32_t block;
    uint32_t bad_count = 0U;
    uint32_t t0 = timebase_millis();

    for (block = 0U; block < NAND_WT_BLOCKS; ++block) {
        uint8_t is_bad = 0U;

        if (nand_mt29f_is_block_bad(block, &is_bad) != BOARD_OK) {
            is_bad = 1U;
        }

        bad_block[block] = is_bad;
        if (is_bad != 0U) {
            ++bad_count;
            if (bad_count <= 16U) {
                log_kv("bad_block", block);
            }
        }
    }

    log_kv("bad_scan_ms", timebase_millis() - t0);
    log_kv("bad_blocks_total", bad_count);

    return bad_count;
}

#if NAND_WT_DO_ERASE
static void phase_erase(void) {
    uint32_t block;
    uint32_t erased = 0U;
    uint32_t erase_fail = 0U;
    uint8_t have_first_fail = 0U;
    uint32_t t0 = timebase_millis();

    for (block = 0U; block < NAND_WT_BLOCKS; ++block) {
        BoardStatus status;

        if (bad_block[block] != 0U) {
            continue;
        }

        status = nand_mt29f_erase_block(block);
        if (status != BOARD_OK) {
            ++erase_fail;
            if (have_first_fail == 0U) {
                have_first_fail = 1U;
                log_kv("first_erase_fail_block", block);
                log_kv("erase_status", (uint32_t)status);
            }
            continue;
        }

        ++erased;
    }

    log_kv("erase_ms", timebase_millis() - t0);
    log_kv("blocks_erased", erased);
    log_kv("erase_fail", erase_fail);
}
#endif

#if NAND_WT_DO_WRITE
static void phase_write(void) {
    uint32_t block;
    uint32_t written = 0U;
    uint32_t write_fail = 0U;
    uint8_t have_first_fail = 0U;
    uint32_t t0 = timebase_millis();

    for (block = 0U; block < NAND_WT_BLOCKS; ++block) {
        uint32_t page;

        if ((block % NAND_WT_PROGRESS_BLOCKS) == 0U) {
            log_kv("write_at_block", block);
        }

        if (bad_block[block] != 0U) {
            continue;
        }

        for (page = 0U; page < NAND_WT_PAGES_PER_BLOCK; ++page) {
            BoardStatus status;

            fill_page(block, page);

            status = nand_mt29f_program_page(block, page, write_buf, NAND_WT_PAGE_SIZE);
            if (status != BOARD_OK) {
                ++write_fail;
                if (have_first_fail == 0U) {
                    have_first_fail = 1U;
                    log_kv("first_write_fail_block", block);
                    log_kv("first_write_fail_page", page);
                    log_kv("write_status", (uint32_t)status);
                }
                continue;
            }

            ++written;
        }
    }

    log_kv("write_ms", timebase_millis() - t0);
    log_kv("pages_written", written);
    log_kv("write_fail", write_fail);
}
#endif

#if NAND_WT_DO_VERIFY
static void phase_verify(void) {
    uint32_t block;
    uint32_t pages_ok = 0U;
    uint32_t pages_fail = 0U;
    uint8_t have_first_fail = 0U;
    uint32_t t0 = timebase_millis();

    for (block = 0U; block < NAND_WT_BLOCKS; ++block) {
        uint32_t page;

        if ((block % NAND_WT_PROGRESS_BLOCKS) == 0U) {
            log_kv("verify_at_block", block);
        }

        if (bad_block[block] != 0U) {
            continue;
        }

        for (page = 0U; page < NAND_WT_PAGES_PER_BLOCK; ++page) {
            BoardStatus status;
            uint32_t offset;
            uint8_t page_ok = 1U;

            fill_page(block, page);

            status = nand_mt29f_read_page(block, page, read_buf, NAND_WT_PAGE_SIZE);
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

            for (offset = 0U; offset < NAND_WT_PAGE_SIZE; ++offset) {
                if (read_buf[offset] != write_buf[offset]) {
                    page_ok = 0U;
                    if (have_first_fail == 0U) {
                        have_first_fail = 1U;
                        log_kv("first_mismatch_block", block);
                        log_kv("first_mismatch_page", page);
                        log_kv("first_mismatch_offset", offset);
                        log_kv("first_mismatch_expected", write_buf[offset]);
                        log_kv("first_mismatch_actual", read_buf[offset]);
                    }
                    break;
                }
            }

            if (page_ok != 0U) {
                ++pages_ok;
            } else {
                ++pages_fail;
            }
        }
    }

    log_kv("verify_ms", timebase_millis() - t0);
    log_kv("pages_ok", pages_ok);
    log_kv("pages_fail", pages_fail);
}
#endif

int main(void) {
    NandMt29fId id;
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand full test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    status = qspi_init();
    log_kv("qspi_init", (uint32_t)status);

    status = qspi_dma_init();
    log_kv("qspi_dma_init", (uint32_t)status);

    log_kv("bank_id", (uint32_t)NAND_WT_BANK_ID);
    log_kv("test_blocks", (uint32_t)NAND_WT_BLOCKS);
    log_kv("do_erase", (uint32_t)NAND_WT_DO_ERASE);
    log_kv("do_write", (uint32_t)NAND_WT_DO_WRITE);
    log_kv("do_verify", (uint32_t)NAND_WT_DO_VERIFY);
    log_kv("cache_read_mode", nand_mt29f_get_cache_read_mode());
    log_kv("program_load_mode", nand_mt29f_get_program_load_mode());
#if defined(QSPI_DMA_READ_ENABLED)
    log_kv("dma_read", QSPI_DMA_READ_ENABLED);
#endif
#if defined(QSPI_DMA_WRITE_ENABLED)
    log_kv("dma_write", QSPI_DMA_WRITE_ENABLED);
#endif

    status = nand_power_bank();
    log_kv("power_drive", (uint32_t)status);
    if (status != BOARD_OK) {
        goto done;
    }

    status = nand_mt29f_select_bank(NAND_WT_BANK);
    log_kv("select_bank", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }

    status = nand_mt29f_read_id(&id);
    log_kv("read_id", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }
    if ((id.manufacturer_id != 0x2CU) || (id.device_id != 0x34U)) {
        log_kv("id_mismatch_mfr", id.manufacturer_id);
        log_kv("id_mismatch_dev", id.device_id);
        goto unpower;
    }

    status = nand_mt29f_init();
    log_kv("nand_init", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }

    (void)scan_bad_blocks();

#if NAND_WT_DO_ERASE
    phase_erase();
#endif
#if NAND_WT_DO_WRITE
    phase_write();
#endif
#if NAND_WT_DO_VERIFY
    phase_verify();
#endif

unpower:
    nand_unpower_bank();

done:
    debug_log_write("\r\nnand full test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
