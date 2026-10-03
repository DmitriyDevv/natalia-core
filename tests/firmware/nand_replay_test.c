/*
 * Последовательности команд nand_storage / режима TEST напрямую через драйвер
 * nand_mt29f_* и BSP QSPI, без nand_storage / Board_API / Algorithm.
 *
 * "mount" ниже = то, что делает nand_storage_mount(): nand_mt29f_select_bank() +
 * nand_mt29f_init() (RESET, A0h/B0h). Пакет = 2048 байт = половина страницы, запись
 * пакета = nand_mt29f_program_page_dma_start_at() + опрос nand_mt29f_program_page_dma_poll(),
 * как в nand_storage.
 *
 * Сценарии (всё проверяется чтением):
 *   A0  стирание блоков подряд без mount;
 *   A1  mount, затем сразу стирание блоков подряд (как nand_storage_erase_bank_start);
 *   A2  mount перед каждым стиранием;
 *       перед A0/A1/A2 в страницы 0 и 63 каждого блока записываются данные,
 *       поэтому невыполненное стирание видно;
 *   B   как режим TEST: на пакет mount -> запись -> mount -> чтение и сравнение;
 *   C   то же без mount;
 *   D   как поток nand_storage: запись пакетов подряд, потом проверка.
 * После B, C, D все пакеты перечитываются ещё раз ("rd").
 *
 * Перед сценариями записывается поведение FLAGB ключа питания FPF2101 (PSON) при включении:
 *   T1  включение сразу после board_init_hardware() (так делает board_nand_power_on());
 *   T2  включение после 200 мс в выключенном состоянии;
 *   T3  штатный board_nand_power_on().
 * FPF2101 (даташит): при перегрузке по току дольше tBLANK = 5..20 мс ключ выключается,
 * FLAGB = LOW на время автоперезапуска tRSTRT = 80..320 мс, затем ключ включается снова.
 * Если board_nand_power_on() сообщает аварию, тест всё равно продолжает, включив ключ
 * напрямую (как nand_full_test), чтобы получить результат по драйверу.
 *
 * DESTRUCTIVE для блоков 1024..1087, 1100..1101, 1110..1111, 1120..1123 банка RP_BANK_ID
 * (по умолчанию 1; -DRP_BANK_ID=2U — банк 2).
 *
 * Сборка:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/nand_replay_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "gpio.h"
#include "nand_mt29f.h"
#include "qspi.h"
#include "status.h"
#include "timebase.h"

#define RP_START_DELAY_MS 5000U
#define RP_GAP_MS 100U
#ifndef RP_BANK_ID
#define RP_BANK_ID 1U
#endif

#if (RP_BANK_ID == 1U)
#define RP_NAND_BANK NAND_MT29F_BANK_1
#define RP_PS_PIN BOARD_PIN_PU_NAND1_PS
#define RP_PSON_PIN BOARD_PIN_PU_NAND1_PSON
#elif (RP_BANK_ID == 2U)
#define RP_NAND_BANK NAND_MT29F_BANK_2
#define RP_PS_PIN BOARD_PIN_PU_NAND2_PS
#define RP_PSON_PIN BOARD_PIN_PU_NAND2_PSON
#else
#error "RP_BANK_ID must be 1 or 2"
#endif

#define RP_PAGE_SIZE NAND_MT29F_PAGE_SIZE
#define RP_PACKET_SIZE 2048UL
#define RP_PACKETS_PER_PAGE (RP_PAGE_SIZE / RP_PACKET_SIZE)
#define RP_PACKETS_PER_BLOCK (NAND_MT29F_PAGES_PER_BLOCK * RP_PACKETS_PER_PAGE)

#define RP_A_FIRST_BLOCK 1024U
#define RP_A_BLOCKS 64U
#define RP_B_FIRST_BLOCK 1100U
#define RP_B_BLOCKS 2U
#define RP_C_FIRST_BLOCK 1110U
#define RP_C_BLOCKS 2U
#define RP_D_FIRST_BLOCK 1120U
#define RP_D_BLOCKS 4U

#define RP_A_PAGE_FIRST 0U
#define RP_A_PAGE_LAST (NAND_MT29F_PAGES_PER_BLOCK - 1U)

#define RP_TAG_FILL_A0 0x10U
#define RP_TAG_FILL_A1 0x11U
#define RP_TAG_FILL_A2 0x12U
#define RP_TAG_B 0x20U
#define RP_TAG_C 0x30U
#define RP_TAG_D 0x40U

/* Status-register ECC field value meaning "uncorrectable" (datasheet table 12). */
#define RP_ECC_UNCORRECTABLE 2U

/* FPF2101: ON input is active low. */
#define RP_PS_ON GPIO_LEVEL_LOW
#define RP_PS_OFF GPIO_LEVEL_HIGH

/* FLAGB trace after switching on: 1 sample per ms. Covers tBLANK (<= 20 ms) + tRSTRT (<= 320 ms). */
#define RP_TRACE_MS 400U
/* board_nand_power_on() reads PSON at this point. */
#define RP_PSON_CHECK_MS 50U
#define RP_POWER_OFF_HOLD_MS 200U

/* Guard against a hung DMA program state machine (poll calls). */
#define RP_PROGRAM_POLL_GUARD 2000000UL

typedef struct {
    uint32_t ops;
    uint32_t err;
    uint32_t bad;
    uint32_t mount_err;
    uint32_t ecc_uncorrectable;
    uint32_t unconfirmed;
    uint32_t min_us;
    uint32_t max_us;
    uint8_t has_fail;
    uint32_t fail_block;
    uint32_t fail_page;
    uint32_t fail_column;
    BoardStatus fail_status;
    NandMt29fFault fail_fault;
    uint8_t fail_chip_status;
    uint32_t fail_bytes;
} ReplayStats;

typedef struct {
    ReplayStats a0;
    ReplayStats a1;
    ReplayStats a2;
    ReplayStats b_prog;
    ReplayStats b_reread;
    ReplayStats c_prog;
    ReplayStats c_reread;
    ReplayStats d_prog;
    ReplayStats d_reread;
    uint32_t prep_err;
    uint8_t passed;
} ReplayResult;

volatile ReplayResult replay_result;

static uint8_t replay_wbuf[RP_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t replay_rbuf[RP_PAGE_SIZE] __attribute__((aligned(4)));

static void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void log_line(const char* format, ...) {
    char text[128];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    debug_log_write(text);
}

static void halt(const char* reason) {
    log_line("STOP: %s\r\n", reason);
    debug_log_write("===== TEST FAILED =====\r\n");

    for (;;) {
    }
}

/* ------------------------------------------------------------------------- */
/* Power switch trace                                                         */
/* ------------------------------------------------------------------------- */

static uint32_t read_flagb(void) {
    GpioLevel level = GPIO_LEVEL_LOW;

    (void)gpio_read(RP_PSON_PIN, &level);

    return (level == GPIO_LEVEL_HIGH) ? 1U : 0U;
}

/* Switch on and record FLAGB once per ms for RP_TRACE_MS ms. The switch stays on. */
static void power_trace(const char* name) {
    uint32_t ms;
    uint32_t low_count = 0U;
    uint32_t first_low = UINT32_MAX;
    uint32_t last_low = 0U;
    uint32_t at_check = 0U;
    uint32_t level;

    (void)gpio_write(RP_PS_PIN, RP_PS_ON);

    for (ms = 0U; ms < RP_TRACE_MS; ++ms) {
        level = read_flagb();

        if (level == 0U) {
            ++low_count;
            if (first_low == UINT32_MAX) {
                first_low = ms;
            }
            last_low = ms;
        }

        if (ms == RP_PSON_CHECK_MS) {
            at_check = level;
        }

        timebase_delay_ms_blocking(1U);
    }

    if (low_count == 0U) {
        log_line("%s: FLAGB always HIGH (%lu ms)\r\n", name, (unsigned long)RP_TRACE_MS);
    } else {
        log_line("%s: FLAGB LOW %lu ms, first %lu ms, last %lu ms, at %lu ms: %lu, at end: %lu\r\n",
                 name,
                 (unsigned long)low_count,
                 (unsigned long)first_low,
                 (unsigned long)last_low,
                 (unsigned long)RP_PSON_CHECK_MS,
                 (unsigned long)at_check,
                 (unsigned long)read_flagb());
    }
}

/* ------------------------------------------------------------------------- */
/* Data                                                                       */
/* ------------------------------------------------------------------------- */

static uint32_t mix32(uint32_t x) {
    x ^= x >> 16U;
    x *= 0x85EBCA6BUL;
    x ^= x >> 13U;
    x *= 0xC2B2AE35UL;
    x ^= x >> 16U;
    return x;
}

/* Unique per (tag, block, page, half): tag -> bits 24..31, block -> 8..18, page -> 1..6, half -> 0. */
static uint32_t replay_seed(uint32_t tag, uint32_t block, uint32_t page, uint32_t half) {
    return (tag << 24U) | (block << 8U) | (page << 1U) | half;
}

static void fill_random(uint8_t* buffer, uint32_t size, uint32_t seed) {
    uint32_t state = mix32(seed);
    uint32_t index;

    if (state == 0U) {
        state = 0x6D2B79F5UL;
    }

    for (index = 0U; index < size; index += 4U) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        (void)memcpy(&buffer[index], &state, 4U);
    }
}

static uint32_t count_diff(const uint8_t* a, const uint8_t* b, uint32_t size) {
    uint32_t diff = 0U;
    uint32_t index;

    if (memcmp(a, b, size) == 0) {
        return 0U;
    }

    for (index = 0U; index < size; ++index) {
        if (a[index] != b[index]) {
            ++diff;
        }
    }

    return diff;
}

static uint32_t count_not_ff(const uint8_t* data, uint32_t size) {
    uint32_t bad = 0U;
    uint32_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] != 0xFFU) {
            ++bad;
        }
    }

    return bad;
}

static void replay_locate(uint32_t first_block, uint32_t index,
                          uint32_t* block, uint32_t* page, uint32_t* half) {
    uint32_t in_block = index % RP_PACKETS_PER_BLOCK;

    *block = first_block + (index / RP_PACKETS_PER_BLOCK);
    *page = in_block / RP_PACKETS_PER_PAGE;
    *half = in_block % RP_PACKETS_PER_PAGE;
}

/* ------------------------------------------------------------------------- */
/* Stats                                                                      */
/* ------------------------------------------------------------------------- */

static void stats_begin(ReplayStats* stats) {
    (void)memset(stats, 0, sizeof(*stats));
    stats->min_us = UINT32_MAX;
}

static void stats_time(ReplayStats* stats, uint32_t us) {
    ++stats->ops;

    if (us < stats->min_us) {
        stats->min_us = us;
    }

    if (us > stats->max_us) {
        stats->max_us = us;
    }
}

static void stats_fail(ReplayStats* stats, uint32_t block, uint32_t page, uint32_t column,
                       BoardStatus status, uint32_t bad_bytes) {
    if (status != BOARD_OK) {
        ++stats->err;
    } else {
        ++stats->bad;
    }

    if (stats->has_fail == 0U) {
        stats->has_fail = 1U;
        stats->fail_block = block;
        stats->fail_page = page;
        stats->fail_column = column;
        stats->fail_status = status;
        stats->fail_fault = nand_mt29f_get_last_fault();
        stats->fail_chip_status = nand_mt29f_get_last_fault_status();
        stats->fail_bytes = bad_bytes;
    }
}

static uint8_t stats_ok(const ReplayStats* stats) {
    return ((stats->err == 0U) && (stats->bad == 0U) && (stats->mount_err == 0U) &&
            (stats->ecc_uncorrectable == 0U)) ? 1U : 0U;
}

static void stats_log(const char* name, const ReplayStats* stats) {
    log_line("%-4s ops %lu, err %lu, bad %lu, mount_err %lu, ecc_unc %lu, unconf %lu, op %lu..%lu us\r\n",
             name,
             (unsigned long)stats->ops,
             (unsigned long)stats->err,
             (unsigned long)stats->bad,
             (unsigned long)stats->mount_err,
             (unsigned long)stats->ecc_uncorrectable,
             (unsigned long)stats->unconfirmed,
             (unsigned long)((stats->ops != 0U) ? stats->min_us : 0U),
             (unsigned long)stats->max_us);

    if (stats->has_fail != 0U) {
        log_line("     first fail: block %lu page %lu col %lu, status %u, fault %u, chip 0x%02X, bad bytes %lu\r\n",
                 (unsigned long)stats->fail_block,
                 (unsigned long)stats->fail_page,
                 (unsigned long)stats->fail_column,
                 (unsigned)stats->fail_status,
                 (unsigned)stats->fail_fault,
                 (unsigned)stats->fail_chip_status,
                 (unsigned long)stats->fail_bytes);
    }
}

/* ------------------------------------------------------------------------- */
/* natalia-core operations                                                    */
/* ------------------------------------------------------------------------- */

/* What nand_storage_mount() does to the chip. */
static BoardStatus replay_mount(void) {
    BoardStatus status;

    status = nand_mt29f_select_bank(RP_NAND_BANK);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_mt29f_init();
}

/* One packet write the way nand_storage does it: DMA start + poll until done. */
static BoardStatus replay_program_packet(uint32_t block, uint32_t page, uint32_t column,
                                         const uint8_t* data, uint32_t size) {
    BoardStatus status;
    uint8_t is_done = 0U;
    uint32_t guard = 0U;

    status = nand_mt29f_program_page_dma_start_at(block, page, column, data, size);
    if (status != BOARD_OK) {
        return status;
    }

    while (is_done == 0U) {
        status = nand_mt29f_program_page_dma_poll(&is_done);
        if (status != BOARD_OK) {
            return status;
        }

        ++guard;
        if (guard >= RP_PROGRAM_POLL_GUARD) {
            return BOARD_ERR_TIMEOUT;
        }
    }

    return BOARD_OK;
}

/* Read with the ECC status checked. */
static BoardStatus replay_read(ReplayStats* stats, uint32_t block, uint32_t page, uint32_t column,
                               uint8_t* data, uint32_t size) {
    BoardStatus status = nand_mt29f_read_page_at(block, page, column, data, size);

    if ((status == BOARD_OK) && (nand_mt29f_get_last_ecc_status() == RP_ECC_UNCORRECTABLE)) {
        ++stats->ecc_uncorrectable;
    }

    return status;
}

/* ------------------------------------------------------------------------- */
/* Preparation                                                                */
/* ------------------------------------------------------------------------- */

static void prep_erase(uint32_t first_block, uint32_t count) {
    uint32_t index;

    for (index = 0U; index < count; ++index) {
        if (nand_mt29f_erase_block(first_block + index) != BOARD_OK) {
            ++replay_result.prep_err;
        }
    }
}

static void prep_fill_a(uint32_t tag) {
    static const uint32_t pages[2] = {RP_A_PAGE_FIRST, RP_A_PAGE_LAST};
    uint32_t index;
    uint32_t k;

    prep_erase(RP_A_FIRST_BLOCK, RP_A_BLOCKS);

    for (index = 0U; index < RP_A_BLOCKS; ++index) {
        uint32_t block = RP_A_FIRST_BLOCK + index;

        for (k = 0U; k < 2U; ++k) {
            fill_random(replay_wbuf, RP_PAGE_SIZE, replay_seed(tag, block, pages[k], 0U));
            if (nand_mt29f_program_page(block, pages[k], replay_wbuf, RP_PAGE_SIZE) != BOARD_OK) {
                ++replay_result.prep_err;
            }
        }
    }
}

static void verify_a_erased(ReplayStats* stats) {
    static const uint32_t pages[2] = {RP_A_PAGE_FIRST, RP_A_PAGE_LAST};
    uint32_t index;
    uint32_t k;

    for (index = 0U; index < RP_A_BLOCKS; ++index) {
        uint32_t block = RP_A_FIRST_BLOCK + index;

        for (k = 0U; k < 2U; ++k) {
            BoardStatus status = replay_read(stats, block, pages[k], 0U, replay_rbuf, RP_PAGE_SIZE);
            uint32_t not_ff = (status == BOARD_OK) ? count_not_ff(replay_rbuf, RP_PAGE_SIZE) : 0U;

            if ((status != BOARD_OK) || (not_ff != 0U)) {
                stats_fail(stats, block, pages[k], 0U, status, not_ff);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Scenarios                                                                  */
/* ------------------------------------------------------------------------- */

/* mount_mode: 0 - none, 1 - once before the burst, 2 - before every erase. */
static void scenario_erase(ReplayStats* stats, uint32_t mount_mode, uint32_t fill_tag) {
    uint32_t index;

    prep_fill_a(fill_tag);
    timebase_delay_ms_blocking(RP_GAP_MS);

    stats_begin(stats);

    if ((mount_mode == 1U) && (replay_mount() != BOARD_OK)) {
        ++stats->mount_err;
    }

    for (index = 0U; index < RP_A_BLOCKS; ++index) {
        uint32_t block = RP_A_FIRST_BLOCK + index;
        uint32_t t0;
        BoardStatus status;

        if ((mount_mode == 2U) && (replay_mount() != BOARD_OK)) {
            ++stats->mount_err;
        }

        t0 = timebase_cycles();
        status = nand_mt29f_erase_block(block);
        stats_time(stats, timebase_us_since(t0));

        if (status != BOARD_OK) {
            stats_fail(stats, block, 0U, 0U, status, 0U);
        }
    }

    timebase_delay_ms_blocking(RP_GAP_MS);
    verify_a_erased(stats);
}

static void reread_all(ReplayStats* stats, uint32_t tag, uint32_t first_block, uint32_t packets) {
    uint32_t index;

    stats_begin(stats);

    for (index = 0U; index < packets; ++index) {
        uint32_t block;
        uint32_t page;
        uint32_t half;
        uint32_t column;
        uint32_t t0;
        uint32_t diff;
        BoardStatus status;

        replay_locate(first_block, index, &block, &page, &half);
        column = half * RP_PACKET_SIZE;
        fill_random(replay_wbuf, RP_PACKET_SIZE, replay_seed(tag, block, page, half));

        t0 = timebase_cycles();
        status = replay_read(stats, block, page, column, replay_rbuf, RP_PACKET_SIZE);
        stats_time(stats, timebase_us_since(t0));

        diff = (status == BOARD_OK) ? count_diff(replay_rbuf, replay_wbuf, RP_PACKET_SIZE) : 0U;
        if ((status != BOARD_OK) || (diff != 0U)) {
            stats_fail(stats, block, page, column, status, diff);
        }
    }
}

/* with_mount != 0: like TEST mode - mount, write, mount, read per packet. */
static void scenario_write_read(ReplayStats* stats, uint32_t tag, uint32_t first_block,
                                uint32_t blocks, uint8_t with_mount) {
    uint32_t packets = blocks * RP_PACKETS_PER_BLOCK;
    uint32_t index;

    stats_begin(stats);

    for (index = 0U; index < packets; ++index) {
        uint32_t block;
        uint32_t page;
        uint32_t half;
        uint32_t column;
        uint32_t t0;
        uint32_t diff;
        BoardStatus status;

        replay_locate(first_block, index, &block, &page, &half);
        column = half * RP_PACKET_SIZE;
        fill_random(replay_wbuf, RP_PACKET_SIZE, replay_seed(tag, block, page, half));

        if ((with_mount != 0U) && (replay_mount() != BOARD_OK)) {
            ++stats->mount_err;
        }

        t0 = timebase_cycles();
        status = replay_program_packet(block, page, column, replay_wbuf, RP_PACKET_SIZE);
        stats_time(stats, timebase_us_since(t0));

        if (nand_mt29f_last_program_unconfirmed() != 0U) {
            ++stats->unconfirmed;
        }

        if (status != BOARD_OK) {
            stats_fail(stats, block, page, column, status, 0U);
            continue;
        }

        if ((with_mount != 0U) && (replay_mount() != BOARD_OK)) {
            ++stats->mount_err;
        }

        status = replay_read(stats, block, page, column, replay_rbuf, RP_PACKET_SIZE);
        diff = (status == BOARD_OK) ? count_diff(replay_rbuf, replay_wbuf, RP_PACKET_SIZE) : 0U;
        if ((status != BOARD_OK) || (diff != 0U)) {
            stats_fail(stats, block, page, column, status, diff);
        }
    }
}

/* Like the nand_storage stream: packet writes back to back, no reads in between. */
static void scenario_write_burst(ReplayStats* stats, uint32_t tag, uint32_t first_block, uint32_t blocks) {
    uint32_t packets = blocks * RP_PACKETS_PER_BLOCK;
    uint32_t index;

    stats_begin(stats);

    for (index = 0U; index < packets; ++index) {
        uint32_t block;
        uint32_t page;
        uint32_t half;
        uint32_t column;
        uint32_t t0;
        BoardStatus status;

        replay_locate(first_block, index, &block, &page, &half);
        column = half * RP_PACKET_SIZE;
        fill_random(replay_wbuf, RP_PACKET_SIZE, replay_seed(tag, block, page, half));

        t0 = timebase_cycles();
        status = replay_program_packet(block, page, column, replay_wbuf, RP_PACKET_SIZE);
        stats_time(stats, timebase_us_since(t0));

        if (nand_mt29f_last_program_unconfirmed() != 0U) {
            ++stats->unconfirmed;
        }

        if (status != BOARD_OK) {
            stats_fail(stats, block, page, column, status, 0U);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */

static void run_and_log(ReplayStats* stats, volatile ReplayStats* slot, const char* name, uint8_t* all_ok) {
    *slot = *stats;
    stats_log(name, stats);
    if (stats_ok(stats) == 0U) {
        *all_ok = 0U;
    }
}

int main(void) {
    ReplayStats stats;
    NandMt29fId id;
    BoardStatus status;
    uint8_t is_powered = 0U;
    uint8_t all_ok = 1U;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(RP_START_DELAY_MS);
    log_line("\r\n===== NAND replay test (natalia-core driver), bank %u =====\r\n", (unsigned)RP_BANK_ID);

    status = board_init_hardware();
    log_line("board_init_hardware=%u\r\n", (unsigned)status);

    log_line("qspi_hz=%lu sshift=%u dma_read=%u dma_write=%u cache_read_mode=%u program_load_mode=%u\r\n",
             (unsigned long)QSPI_TARGET_HZ,
             (unsigned)QSPI_SAMPLE_SHIFT,
             (unsigned)QSPI_DMA_READ_ENABLED,
             (unsigned)QSPI_DMA_WRITE_ENABLED,
             (unsigned)nand_mt29f_get_cache_read_mode(),
             (unsigned)nand_mt29f_get_program_load_mode());

    /* --- Power switch behaviour ------------------------------------------ */
    log_line("FLAGB before power on: %lu\r\n", (unsigned long)read_flagb());

    power_trace("T1 on right after board_init");
    (void)gpio_write(RP_PS_PIN, RP_PS_OFF);
    timebase_delay_ms_blocking(RP_POWER_OFF_HOLD_MS);

    power_trace("T2 on after 200 ms off");
    (void)gpio_write(RP_PS_PIN, RP_PS_OFF);
    timebase_delay_ms_blocking(RP_POWER_OFF_HOLD_MS);

    status = board_nand_power_on(RP_BANK_ID);
    (void)board_nand_is_powered(RP_BANK_ID, &is_powered);
    log_line("T3 board_nand_power_on=%u is_powered=%u\r\n", (unsigned)status, (unsigned)is_powered);

    if ((status != BOARD_OK) || (is_powered == 0U)) {
        log_line("board_nand_power_on reports a fault: continuing with PS driven directly\r\n");
        (void)gpio_write(RP_PS_PIN, RP_PS_ON);
        timebase_delay_ms_blocking(RP_TRACE_MS);
        log_line("FLAGB after direct on: %lu\r\n", (unsigned long)read_flagb());
    }

    status = qspi_init();
    if (status == BOARD_OK) {
        status = qspi_dma_init();
    }
    if (status == BOARD_OK) {
        status = nand_mt29f_select_bank(RP_NAND_BANK);
    }
    if (status == BOARD_OK) {
        status = nand_mt29f_read_id(&id);
    }
    log_line("read_id=%u mfr=0x%02X dev=0x%02X\r\n", (unsigned)status,
             (unsigned)id.manufacturer_id, (unsigned)id.device_id);
    if (status != BOARD_OK) {
        halt("QSPI init / READ ID failed");
    }

    status = nand_mt29f_init();
    log_line("nand_init=%u\r\n", (unsigned)status);
    if (status != BOARD_OK) {
        halt("nand_mt29f_init failed");
    }

    /* --- A: erase --------------------------------------------------------- */
    scenario_erase(&stats, 0U, RP_TAG_FILL_A0);
    log_line("A0 erase burst, no mount:\r\n");
    run_and_log(&stats, &replay_result.a0, "A0", &all_ok);

    scenario_erase(&stats, 1U, RP_TAG_FILL_A1);
    log_line("A1 mount, then erase burst:\r\n");
    run_and_log(&stats, &replay_result.a1, "A1", &all_ok);

    scenario_erase(&stats, 2U, RP_TAG_FILL_A2);
    log_line("A2 mount before each erase:\r\n");
    run_and_log(&stats, &replay_result.a2, "A2", &all_ok);

    /* --- B: like TEST mode -------------------------------------------------- */
    prep_erase(RP_B_FIRST_BLOCK, RP_B_BLOCKS);
    timebase_delay_ms_blocking(RP_GAP_MS);
    scenario_write_read(&stats, RP_TAG_B, RP_B_FIRST_BLOCK, RP_B_BLOCKS, 1U);
    log_line("B  mount, write, mount, read (TEST mode):\r\n");
    run_and_log(&stats, &replay_result.b_prog, "B", &all_ok);

    timebase_delay_ms_blocking(RP_GAP_MS);
    reread_all(&stats, RP_TAG_B, RP_B_FIRST_BLOCK, RP_B_BLOCKS * RP_PACKETS_PER_BLOCK);
    run_and_log(&stats, &replay_result.b_reread, "B rd", &all_ok);

    /* --- C: same without mount ---------------------------------------------- */
    prep_erase(RP_C_FIRST_BLOCK, RP_C_BLOCKS);
    timebase_delay_ms_blocking(RP_GAP_MS);
    scenario_write_read(&stats, RP_TAG_C, RP_C_FIRST_BLOCK, RP_C_BLOCKS, 0U);
    log_line("C  write, read, no mount:\r\n");
    run_and_log(&stats, &replay_result.c_prog, "C", &all_ok);

    timebase_delay_ms_blocking(RP_GAP_MS);
    reread_all(&stats, RP_TAG_C, RP_C_FIRST_BLOCK, RP_C_BLOCKS * RP_PACKETS_PER_BLOCK);
    run_and_log(&stats, &replay_result.c_reread, "C rd", &all_ok);

    /* --- D: nand_storage stream --------------------------------------------- */
    prep_erase(RP_D_FIRST_BLOCK, RP_D_BLOCKS);
    timebase_delay_ms_blocking(RP_GAP_MS);
    scenario_write_burst(&stats, RP_TAG_D, RP_D_FIRST_BLOCK, RP_D_BLOCKS);
    log_line("D  packet writes in a row (nand_storage stream):\r\n");
    run_and_log(&stats, &replay_result.d_prog, "D", &all_ok);

    timebase_delay_ms_blocking(RP_GAP_MS);
    reread_all(&stats, RP_TAG_D, RP_D_FIRST_BLOCK, RP_D_BLOCKS * RP_PACKETS_PER_BLOCK);
    run_and_log(&stats, &replay_result.d_reread, "D rd", &all_ok);

    log_line("prep_err=%lu\r\n", (unsigned long)replay_result.prep_err);
    if (replay_result.prep_err != 0U) {
        all_ok = 0U;
    }

    replay_result.passed = all_ok;
    debug_log_write((all_ok != 0U) ? "===== TEST PASSED =====\r\n" : "===== TEST FAILED =====\r\n");

    for (;;) {
    }
}
