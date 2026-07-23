/*
 * QSPI NAND read-latency benchmark. Non-destructive (reads page 0/0 only).
 *
 * nand_mt29f_read_page loads the full page into the chip cache regardless of the
 * requested size, then transfers only <size> bytes over QSPI. Timing read_page
 * at several sizes therefore separates the fixed per-page overhead (page-read
 * command + wait_ready) from the QSPI data-transfer time:
 *
 *   us(size)  ~= fixed_overhead + transfer(size)
 *   fixed     ~= us(small)
 *   per_byte  ~= (us(4096) - us(small)) / (4096 - small)
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

#ifndef NAND_BENCH_BANK_ID
#define NAND_BENCH_BANK_ID 1
#endif

#if (NAND_BENCH_BANK_ID == 1)
#define NAND_BENCH_BANK NAND_MT29F_BANK_1
#define NAND_BENCH_PS_PIN BOARD_PIN_PU_NAND1_PS
#else
#define NAND_BENCH_BANK NAND_MT29F_BANK_2
#define NAND_BENCH_PS_PIN BOARD_PIN_PU_NAND2_PS
#endif

#define NAND_BENCH_ITERS 300U

static uint8_t bench_buf[NAND_MT29F_PAGE_SIZE];

static const uint32_t bench_sizes[] = { 16U, 256U, 1024U, 2048U, 4096U };

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static BoardStatus nand_power_bank(void) {
    const GpioConfig ps_on = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    BoardStatus status;

    status = gpio_configure(NAND_BENCH_PS_PIN, &ps_on);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_write(NAND_BENCH_PS_PIN, GPIO_LEVEL_LOW);
    timebase_delay_ms_blocking(100U);

    return status;
}

static void nand_unpower_bank(void) {
    (void)gpio_write(NAND_BENCH_PS_PIN, GPIO_LEVEL_HIGH);
    timebase_delay_ms_blocking(100U);
}

static void bench_read_size(uint32_t size) {
    uint32_t i;
    uint32_t t0;
    uint32_t total_ms;
    BoardStatus status = BOARD_OK;

    t0 = timebase_millis();
    for (i = 0U; i < NAND_BENCH_ITERS; ++i) {
        status = nand_mt29f_read_page(0U, 0U, bench_buf, size);
        if (status != BOARD_OK) {
            break;
        }
    }
    total_ms = timebase_millis() - t0;

    debug_log_write("size=");
    debug_log_write_u32_inline(size);
    debug_log_write(" total_ms=");
    debug_log_write_u32_inline(total_ms);
    debug_log_write(" us_per_read=");
    debug_log_write_u32_inline((total_ms * 1000U) / NAND_BENCH_ITERS);
    debug_log_write(" status=");
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");
}

int main(void) {
    NandMt29fId id;
    BoardStatus status;
    uint32_t idx;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nnand bench test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    status = qspi_init();
    log_kv("qspi_init", (uint32_t)status);

    status = qspi_dma_init();
    log_kv("qspi_dma_init", (uint32_t)status);

    log_kv("iters", NAND_BENCH_ITERS);
    log_kv("cache_read_mode", nand_mt29f_get_cache_read_mode());
#if defined(QSPI_DMA_READ_ENABLED)
    log_kv("dma_read", QSPI_DMA_READ_ENABLED);
#endif

    status = nand_power_bank();
    log_kv("power_drive", (uint32_t)status);
    if (status != BOARD_OK) {
        goto done;
    }

    status = nand_mt29f_select_bank(NAND_BENCH_BANK);
    log_kv("select_bank", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }

    status = nand_mt29f_read_id(&id);
    log_kv("read_id", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }

    status = nand_mt29f_init();
    log_kv("nand_init", (uint32_t)status);
    if (status != BOARD_OK) {
        goto unpower;
    }

    for (idx = 0U; idx < (sizeof(bench_sizes) / sizeof(bench_sizes[0])); ++idx) {
        bench_read_size(bench_sizes[idx]);
    }

unpower:
    nand_unpower_bank();

done:
    debug_log_write("\r\nnand bench test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
