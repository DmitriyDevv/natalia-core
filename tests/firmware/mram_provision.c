/*
 * MRAM provisioning main.
 *
 * Writes a clean default persistent image to both MRAM copies so the firmware
 * boots into DUTY instead of ALARM. Use it after a destructive MRAM test (e.g.
 * mram_store_test / mram_full_test) has scribbled alarm bits into the store, or
 * on a fresh unit before the ground station provisions real settings with
 * CMD_SET_CFG.
 *
 * The default image is:
 *   - config: all thresholds/belt/rate/counters zero, init_rtc_time 0,
 *     can_control 0, alarm_mask = sanitized ALARM_ALL_MASK, config_version 1;
 *   - service data: all zero (alarm_status 0, no NAND-full, zero counters);
 *   - per-bank NAND test results: zeroed.
 *
 * With alarm_status == 0 the masked alarm is 0 regardless of the mask, so
 * INIT -> DUTY. This is the bring-up analogue of CMD_SET_CFG; it is not the
 * flight path.
 *
 * Requires NATALIA_ENABLE_SPI_DRIVER=ON and NATALIA_ENABLE_MRAM_DRIVER=ON and a
 * firmware (non-stub) build. Flash once, then flash the real main.
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/mram_provision.c
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "mram_store.h"
#include "status.h"
#include "timebase.h"

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

static void write_defaults(void) {
    MramStoreConfig config;
    MramStoreServiceData service;
    static MramStoreTestResult test_result;

    (void)memset(&config, 0, sizeof(config));
    config.alarm_mask = (uint16_t)(alarm_sanitize_mask(ALARM_ALL_MASK) & 0xFFFFU);
    config.config_version = 1U;
    expect_ok("save_config", mram_store_save_config(&config));

    (void)memset(&service, 0, sizeof(service));
    expect_ok("save_service", mram_store_save_service_data(&service));

    (void)memset(&test_result, 0, sizeof(test_result));
    test_result.bank = 1U;
    expect_ok("save_test_result_b1", mram_store_save_test_result(&test_result));
    test_result.bank = 2U;
    expect_ok("save_test_result_b2", mram_store_save_test_result(&test_result));
}

static void verify_defaults(void) {
    MramStoreStatus status;
    MramStoreConfig config;
    MramStoreServiceData service;

    (void)memset(&status, 0, sizeof(status));
    expect_ok("check", mram_store_check(&status));
    expect_true("copy1_valid", status.copy1_valid != 0U);
    expect_true("copy2_valid", status.copy2_valid != 0U);

    (void)memset(&config, 0, sizeof(config));
    expect_ok("load_config", mram_store_load_config(&config));
    log_kv("alarm_mask", (uint32_t)config.alarm_mask);

    (void)memset(&service, 0, sizeof(service));
    expect_ok("load_service", mram_store_load_service_data(&service));
    log_kv("alarm_status", (uint32_t)service.alarm_status);
    expect_true("alarm_status_zero", service.alarm_status == 0U);
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nmram provision start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    write_defaults();
    verify_defaults();

    log_kv("failures", g_failures);
    debug_log_write((g_failures == 0U) ? "RESULT=PASS\r\n" : "RESULT=FAIL\r\n");
    debug_log_write("\r\nmram provision done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
