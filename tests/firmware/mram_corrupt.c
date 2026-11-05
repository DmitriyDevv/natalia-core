/*
 * MRAM corruption for the boot read procedure, block map and test result tests.
 *
 * Inverts two bytes of the selected area in the selected copies at the driver
 * level, without updating the stored CRC, so the copy no longer passes its CRC
 * check. Nothing else is written. The main firmware then has to restore the bad
 * copy from the good one, raise ALARM_MRAM when both store copies are bad,
 * rebuild a NAND block map when both map copies are bad, or refuse to send a
 * test result from a bad copy.
 *
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..."):
 *   MC_COPIES   1 = copy 1, 2 = copy 2, 3 = both copies (default 1)
 *   MC_TARGET   0 = settings/service store, field config_version (default)
 *               1 = NAND1 block map, its CRC
 *               2 = NAND2 block map, its CRC
 *               3 = NAND1 test result, its CRC
 *               4 = NAND2 test result, its CRC
 *
 * Log lines: "mram corrupt start", board_init_hardware=<status>, target=<n>,
 * for every selected copy c<copy>_crc_valid_before, c<copy>_corrupt=<status>,
 * c<copy>_crc_valid_after, then "mram corrupt done".
 *
 * Requires NATALIA_ENABLE_SPI_DRIVER=ON and NATALIA_ENABLE_MRAM_DRIVER=ON.
 */

#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "mram.h"
#include "mram_store.h"
#include "status.h"
#include "timebase.h"

#ifndef MC_COPIES
#define MC_COPIES 1
#endif

#ifndef MC_TARGET
#define MC_TARGET 0
#endif

#if (MC_COPIES < 1) || (MC_COPIES > 3)
#error "MC_COPIES must be 1, 2 or 3"
#endif

#if (MC_TARGET < 0) || (MC_TARGET > 4)
#error "MC_TARGET must be 0..4"
#endif

#define MC_START_DELAY_MS     5000UL
#define MC_COPY_COUNT         2U
#define MC_BLOCK_MAP1_OFFSET  0x6000UL
#define MC_BLOCK_MAP2_OFFSET  0x6400UL
#define MC_TEST_RESULT1_OFFSET 0x2000UL
#define MC_TEST_RESULT2_OFFSET 0x4000UL

#if (MC_TARGET == 1) || (MC_TARGET == 2)
static BoardNandBlockMap g_map;
#elif (MC_TARGET == 3) || (MC_TARGET == 4)
static uint8_t g_test_result[BOARD_MRAM_TEST_RESULT_SIZE];
#endif

static void log_kv(const char *key, uint32_t value) {
    debug_log_write(key);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_copy_kv(uint8_t copy_id, const char *key, uint32_t value) {
    debug_log_write("c");
    debug_log_write_u32_inline(copy_id);
    debug_log_write("_");
    log_kv(key, value);
}

static uint32_t target_address(void) {
#if MC_TARGET == 0
    return (uint32_t)offsetof(MramStoreConfig, config_version);
#elif MC_TARGET == 1
    return MC_BLOCK_MAP1_OFFSET + (uint32_t)sizeof(BoardNandBlockMap);
#elif MC_TARGET == 2
    return MC_BLOCK_MAP2_OFFSET + (uint32_t)sizeof(BoardNandBlockMap);
#elif MC_TARGET == 3
    return MC_TEST_RESULT1_OFFSET + BOARD_MRAM_TEST_RESULT_SIZE;
#else
    return MC_TEST_RESULT2_OFFSET + BOARD_MRAM_TEST_RESULT_SIZE;
#endif
}

static BoardStatus check_copy(uint8_t copy_id, uint8_t *is_valid) {
#if MC_TARGET == 0
    return board_mram_check_crc(copy_id, is_valid);
#elif (MC_TARGET == 1) || (MC_TARGET == 2)
    return board_mram_read_block_map(copy_id, (uint8_t)MC_TARGET, &g_map, is_valid);
#else
    return board_mram_read_test_result(copy_id, (uint8_t)(MC_TARGET - 2), g_test_result,
                                       sizeof(g_test_result), is_valid, NULL);
#endif
}

static void corrupt_copy(uint8_t copy_id) {
    MramBank bank = (copy_id == 1U) ? MRAM_BANK_1 : MRAM_BANK_2;
    uint8_t bytes[2] = {0U, 0U};
    uint8_t is_valid = 0U;
    BoardStatus status;

    status = check_copy(copy_id, &is_valid);
    log_copy_kv(copy_id, "crc_valid_before", (status == BOARD_OK) ? is_valid : 0U);

    status = mram_read(bank, target_address(), bytes, sizeof(bytes));
    if (status == BOARD_OK) {
        bytes[0] = (uint8_t)~bytes[0];
        bytes[1] = (uint8_t)~bytes[1];
        status = mram_write(bank, target_address(), bytes, sizeof(bytes));
    }
    log_copy_kv(copy_id, "corrupt", (uint32_t)status);

    is_valid = 1U;
    status = check_copy(copy_id, &is_valid);
    log_copy_kv(copy_id, "crc_valid_after", (status == BOARD_OK) ? is_valid : 1U);
}

int main(void) {
    BoardStatus status;
    uint8_t copy_id;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(MC_START_DELAY_MS);

    debug_log_write("\r\nmram corrupt start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);
    log_kv("target", (uint32_t)MC_TARGET);

    for (copy_id = 1U; copy_id <= MC_COPY_COUNT; ++copy_id) {
        if (((uint32_t)MC_COPIES & (1UL << (copy_id - 1U))) != 0U) {
            corrupt_copy(copy_id);
        }
    }

    debug_log_write("\r\nmram corrupt done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
