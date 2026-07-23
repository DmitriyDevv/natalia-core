/*
 * On-hardware test for the MRAM persistent store path:
 *   Algorithm mram_store  ->  Board_API board_mram_*  ->  MRAM driver  ->  SPI.
 *
 * Exercises the pieces that mram_full_test.c does NOT cover, because that test
 * talks to the raw MRAM driver and bypasses Board_API:
 *   - board_mram_write/read round-trip with the Board_API region layout
 *   - CRC16 write-and-verify (board_mram_check_crc reports valid after a write)
 *   - mram_store_save/load_config and save/load_service_data round-trips
 *   - mram_store_save_test_result + both-copies-valid accounting
 *   - hot-redundant recovery: corrupt one copy at the driver level (so its CRC
 *     no longer matches), confirm check_crc flags it, then restore from the good
 *     copy and confirm the store still loads the original data.
 *a
 * DESTRUCTIVE for the first ~1 KB region of both MRAM banks (the store region).
 *
 * Requires NATALIA_ENABLE_SPI_DRIVER=ON and NATALIA_ENABLE_MRAM_DRIVER=ON, and
 * a firmware (non-stub) build so the real board_mram_* implementation is linked.
 * Build by pointing the firmware main at this file:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/mram_store_test.c
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "mram.h"
#include "mram_store.h"
#include "status.h"
#include "timebase.h"

#define MRAM_STORE_COPY_1 1U
#define MRAM_STORE_COPY_2 2U

static uint32_t g_failures;

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void expect_ok(const char *what, BoardStatus status) {
    log_kv(what, (uint32_t)status);
    if (status != BOARD_OK) {
        ++g_failures;
    }
}

static void expect_true(const char *what, int condition) {
    log_kv(what, (uint32_t)(condition != 0));
    if (condition == 0) {
        ++g_failures;
    }
}

static void test_board_mram_roundtrip(void) {
    static const uint8_t pattern[8] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U
    };
    uint8_t readback[8];
    uint8_t valid;

    debug_log_write("\r\n--- board_mram round-trip ---\r\n");

    expect_ok("write_copy1", board_mram_write(MRAM_STORE_COPY_1, 0U, pattern, sizeof(pattern)));
    expect_ok("write_copy2", board_mram_write(MRAM_STORE_COPY_2, 0U, pattern, sizeof(pattern)));

    valid = 0U;
    expect_ok("check_crc_copy1", board_mram_check_crc(MRAM_STORE_COPY_1, &valid));
    expect_true("copy1_valid", valid);

    valid = 0U;
    expect_ok("check_crc_copy2", board_mram_check_crc(MRAM_STORE_COPY_2, &valid));
    expect_true("copy2_valid", valid);

    (void)memset(readback, 0, sizeof(readback));
    expect_ok("read_copy1", board_mram_read(MRAM_STORE_COPY_1, 0U, readback, sizeof(readback)));
    expect_true("copy1_data_match", memcmp(pattern, readback, sizeof(pattern)) == 0);

    (void)memset(readback, 0, sizeof(readback));
    expect_ok("read_copy2", board_mram_read(MRAM_STORE_COPY_2, 0U, readback, sizeof(readback)));
    expect_true("copy2_data_match", memcmp(pattern, readback, sizeof(pattern)) == 0);
}

static void test_store_config(void) {
    MramStoreConfig config;
    MramStoreConfig loaded;
    MramStoreStatus status;

    debug_log_write("\r\n--- mram_store config ---\r\n");

    config.alarm_mask = 0xA5A5A5A5UL;
    config.config_version = 0x12345678UL;

    expect_ok("save_config", mram_store_save_config(&config));

    status.copy1_valid = 0U;
    status.copy2_valid = 0U;
    expect_ok("check_after_config", mram_store_check(&status));
    expect_true("config_copy1_valid", status.copy1_valid != 0U);
    expect_true("config_copy2_valid", status.copy2_valid != 0U);

    (void)memset(&loaded, 0, sizeof(loaded));
    expect_ok("load_config", mram_store_load_config(&loaded));
    expect_true("config_match", memcmp(&config, &loaded, sizeof(config)) == 0);
}

static void test_store_service_data(void) {
    MramStoreServiceData service;
    MramStoreServiceData loaded;

    debug_log_write("\r\n--- mram_store service data ---\r\n");

    service.alarm_status = 0xDEADBEEFUL;
    service.nand1_full = 1U;
    service.nand2_full = 0U;
    service.last_test_status = 0x0000CAFEUL;

    expect_ok("save_service", mram_store_save_service_data(&service));

    (void)memset(&loaded, 0, sizeof(loaded));
    expect_ok("load_service", mram_store_load_service_data(&loaded));
    expect_true("service_match", memcmp(&service, &loaded, sizeof(service)) == 0);
}

static void test_store_test_result(void) {
    MramStoreTestResult result;
    MramStoreStatus status;
    uint16_t i;

    debug_log_write("\r\n--- mram_store test result ---\r\n");

    (void)memset(&result, 0, sizeof(result));
    result.bank = 1U;
    result.status = 0x11223344UL;
    result.total_errors = 7UL;
    result.failed_address = 0x000ABCDEUL;
    for (i = 0U; i < TEST_MODE_BLOCK_COUNT; ++i) {
        result.nerr[i] = (uint16_t)(i * 3U);
    }

    expect_ok("save_test_result", mram_store_save_test_result(&result));

    status.copy1_valid = 0U;
    status.copy2_valid = 0U;
    expect_ok("check_after_test_result", mram_store_check(&status));
    expect_true("test_result_copy1_valid", status.copy1_valid != 0U);
    expect_true("test_result_copy2_valid", status.copy2_valid != 0U);
}

static void test_redundant_restore(void) {
    static const uint8_t corruption[8] = {
        0xDEU, 0xADU, 0xBEU, 0xEFU, 0xDEU, 0xADU, 0xBEU, 0xEFU
    };
    MramStoreConfig config;
    MramStoreConfig loaded;
    uint8_t valid;

    debug_log_write("\r\n--- redundant restore ---\r\n");

    config.alarm_mask = 0x0F0F0F0FUL;
    config.config_version = 0x0BADC0DEUL;
    expect_ok("save_config_for_restore", mram_store_save_config(&config));

    /* Corrupt copy 1 at the driver level (bank 1) without touching its stored
     * CRC, so board_mram_check_crc must now flag copy 1 as invalid. */
    expect_ok("corrupt_copy1", mram_write(MRAM_BANK_1, 0U, corruption, sizeof(corruption)));

    valid = 1U;
    expect_ok("check_corrupt_copy1", board_mram_check_crc(MRAM_STORE_COPY_1, &valid));
    expect_true("copy1_invalid_after_corrupt", valid == 0U);

    valid = 0U;
    expect_ok("check_copy2_still_valid", board_mram_check_crc(MRAM_STORE_COPY_2, &valid));
    expect_true("copy2_valid_after_corrupt", valid != 0U);

    expect_ok("restore_redundant", mram_store_restore_redundant_copy());

    valid = 0U;
    expect_ok("check_copy1_after_restore", board_mram_check_crc(MRAM_STORE_COPY_1, &valid));
    expect_true("copy1_valid_after_restore", valid != 0U);

    (void)memset(&loaded, 0, sizeof(loaded));
    expect_ok("load_config_after_restore", mram_store_load_config(&loaded));
    expect_true("config_match_after_restore", memcmp(&config, &loaded, sizeof(config)) == 0);
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nmram store test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    test_board_mram_roundtrip();
    test_store_config();
    test_store_service_data();
    test_store_test_result();
    test_redundant_restore();

    log_kv("failures", g_failures);
    debug_log_write((g_failures == 0U) ? "RESULT=PASS\r\n" : "RESULT=FAIL\r\n");
    debug_log_write("\r\nmram store test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
