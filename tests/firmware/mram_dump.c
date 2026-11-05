/*
 * MRAM read-only dump.
 *
 * Prints the persistent store of both MRAM copies field by field, without
 * writing anything: CRC status of each copy, MramStoreConfig, MramStoreServiceData,
 * NAND test results (Nerr) and NAND block maps per copy and per bank. Used to see
 * exactly what the firmware has saved (addresses, alarm status, counters) before
 * and after a power cycle or a mode transition.
 *
 * Each copy is read directly by copy id, so a copy with a bad CRC is still shown
 * and the store is not "repaired" by the read (mram_store_load_* would restore
 * the redundant copy).
 *
 * Log lines are "c<copy>_<field>=<value>"; blocks with a bad CRC are printed too
 * and marked by c<copy>_crc_valid=0.
 *
 * Requires NATALIA_ENABLE_SPI_DRIVER=ON and NATALIA_ENABLE_MRAM_DRIVER=ON and a
 * firmware (non-stub) build.
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/mram_dump.c
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "mram_store.h"
#include "status.h"
#include "timebase.h"

#define MD_START_DELAY_MS     5000UL
#define MD_CONFIG_OFFSET      0x0000U
#define MD_SERVICE_OFFSET     0x0100U
#define MD_COPY_COUNT         2U
#define MD_BANK_COUNT         2U

static char g_prefix[8];
static uint8_t g_nerr[BOARD_MRAM_TEST_RESULT_SIZE];
static BoardNandBlockMap g_map;

static void set_prefix(uint8_t copy_id) {
    g_prefix[0] = 'c';
    g_prefix[1] = (char)('0' + copy_id);
    g_prefix[2] = '_';
    g_prefix[3] = '\0';
}

static void log_key(const char *key) {
    debug_log_write(g_prefix);
    debug_log_write(key);
    debug_log_write("=");
}

static void log_u32(const char *key, uint32_t value) {
    log_key(key);
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_i32(const char *key, int32_t value) {
    log_key(key);
    if (value < 0) {
        debug_log_write("-");
        debug_log_write_u32_inline((uint32_t)(-(int64_t)value));
    } else {
        debug_log_write_u32_inline((uint32_t)value);
    }
    debug_log_write("\r\n");
}

static void log_hex16(const char *key, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[7];

    text[0] = '0';
    text[1] = 'x';
    text[2] = digits[(value >> 12U) & 0xFU];
    text[3] = digits[(value >> 8U) & 0xFU];
    text[4] = digits[(value >> 4U) & 0xFU];
    text[5] = digits[value & 0xFU];
    text[6] = '\0';

    log_key(key);
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void dump_config(uint8_t copy_id) {
    MramStoreConfig c;
    BoardStatus status;

    (void)memset(&c, 0, sizeof(c));
    status = board_mram_read(copy_id, MD_CONFIG_OFFSET, &c, sizeof(c));
    log_u32("read_config", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    log_i32("mcu_pu_temp_min", c.mcu_pu_temp_min);
    log_i32("mcu_pu_temp_max", c.mcu_pu_temp_max);
    log_i32("pu_temp_min", c.pu_temp_min);
    log_i32("pu_temp_max", c.pu_temp_max);
    log_i32("ped_temp_min", c.ped_temp_min);
    log_i32("ped_temp_max", c.ped_temp_max);
    log_i32("det_temp_min", c.det_temp_min);
    log_i32("det_temp_max", c.det_temp_max);
    log_u32("pu_voltage_min", c.pu_voltage_min);
    log_u32("pu_voltage_max", c.pu_voltage_max);
    log_u32("pu_current_min", c.pu_current_min);
    log_u32("pu_current_max", c.pu_current_max);
    log_u32("ped_voltage_min", c.ped_voltage_min);
    log_u32("ped_voltage_max", c.ped_voltage_max);
    log_u32("ped_current_min", c.ped_current_min);
    log_u32("ped_current_max", c.ped_current_max);
    log_i32("belt_lmin", c.belt_lmin);
    log_i32("belt_lmax", c.belt_lmax);
    log_i32("belt_bmin", c.belt_bmin);
    log_u32("ac1_rate_max", c.ac1_rate_max);
    log_u32("init_rtc_time", c.init_rtc_time);
    log_u32("init_rtc_time_ms", c.init_rtc_time_ms);
    log_hex16("can_control", c.can_control);
    log_hex16("alarm_mask", c.alarm_mask);
    log_u32("config_version", c.config_version);
    log_hex16("destination_id", c.destination_id);
    log_hex16("device_id", c.device_id);
}

static void dump_service(uint8_t copy_id) {
    MramStoreServiceData s;
    BoardStatus status;

    (void)memset(&s, 0, sizeof(s));
    status = board_mram_read(copy_id, MD_SERVICE_OFFSET, &s, sizeof(s));
    log_u32("read_service", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    log_hex16("alarm_status", s.alarm_status);
    log_u32("nand1_full", s.nand1_full);
    log_u32("nand2_full", s.nand2_full);
    log_u32("last_test_status", s.last_test_status);
    log_u32("observe_session_id", s.observe_session_id);
    log_u32("nand1_packet_count", s.nand1_packet_count);
    log_u32("nand2_packet_count", s.nand2_packet_count);
    log_u32("nand1_erase_count", s.nand1_erase_count);
    log_u32("nand2_erase_count", s.nand2_erase_count);
    log_u32("nand1_test_count", s.nand1_test_count);
    log_u32("nand2_test_count", s.nand2_test_count);
    log_u32("nand1_last_dumped_packet", s.nand1_last_dumped_packet);
    log_u32("nand2_last_dumped_packet", s.nand2_last_dumped_packet);
    log_hex16("nand1_last_packet_crc", s.nand1_last_packet_crc);
    log_hex16("nand2_last_packet_crc", s.nand2_last_packet_crc);
}

static void dump_test_result(uint8_t copy_id, uint8_t bank) {
    uint8_t is_valid = 0U;
    uint8_t crc[BOARD_MRAM_TEST_RESULT_CRC_SIZE] = {0U, 0U};
    uint32_t blocks_with_errors = 0U;
    uint32_t nerr_max = 0U;
    uint32_t i;
    BoardStatus status;

    debug_log_write(g_prefix);
    debug_log_write("test_result_b");
    debug_log_write_u32_inline(bank);
    debug_log_write(":\r\n");

    status = board_mram_read_test_result(copy_id, bank, g_nerr, sizeof(g_nerr), &is_valid, crc);
    log_u32("read_test_result", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    for (i = 0U; i < (BOARD_MRAM_TEST_RESULT_SIZE / 3U); ++i) {
        uint32_t nerr = (uint32_t)g_nerr[(i * 3U) + 0U] |
                        ((uint32_t)g_nerr[(i * 3U) + 1U] << 8U) |
                        ((uint32_t)g_nerr[(i * 3U) + 2U] << 16U);
        if (nerr != 0U) {
            ++blocks_with_errors;
        }
        if (nerr > nerr_max) {
            nerr_max = nerr;
        }
    }

    log_u32("test_result_crc_valid", is_valid);
    log_hex16("test_result_crc", (uint32_t)crc[0] | ((uint32_t)crc[1] << 8U));
    log_u32("test_result_blocks_with_errors", blocks_with_errors);
    log_u32("test_result_nerr_max", nerr_max);
}

static uint32_t count_bits(const uint8_t *bytes, uint32_t size) {
    uint32_t count = 0U;
    uint32_t i;

    for (i = 0U; i < size; ++i) {
        uint8_t value = bytes[i];
        while (value != 0U) {
            count += (uint32_t)(value & 1U);
            value = (uint8_t)(value >> 1U);
        }
    }
    return count;
}

static void dump_block_map(uint8_t copy_id, uint8_t bank) {
    uint8_t is_valid = 0U;
    BoardStatus status;

    debug_log_write(g_prefix);
    debug_log_write("block_map_b");
    debug_log_write_u32_inline(bank);
    debug_log_write(":\r\n");

    (void)memset(&g_map, 0, sizeof(g_map));
    status = board_mram_read_block_map(copy_id, bank, &g_map, &is_valid);
    log_u32("read_block_map", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    log_u32("block_map_crc_valid", is_valid);
    log_u32("block_map_bad", count_bits(g_map.bad, BOARD_NAND_BLOCK_MAP_BYTES));
    log_u32("block_map_candidate", count_bits(g_map.candidate, BOARD_NAND_BLOCK_MAP_BYTES));
}

static void dump_copy(uint8_t copy_id) {
    uint8_t is_valid = 0U;
    uint8_t bank;
    BoardStatus status;

    set_prefix(copy_id);
    debug_log_write("\r\n--- copy ");
    debug_log_write_u32_inline(copy_id);
    debug_log_write(" ---\r\n");

    status = board_mram_check_crc(copy_id, &is_valid);
    log_u32("check_crc", (uint32_t)status);
    log_u32("crc_valid", is_valid);

    dump_config(copy_id);
    dump_service(copy_id);

    for (bank = 1U; bank <= MD_BANK_COUNT; ++bank) {
        dump_test_result(copy_id, bank);
    }
    for (bank = 1U; bank <= MD_BANK_COUNT; ++bank) {
        dump_block_map(copy_id, bank);
    }
}

int main(void) {
    BoardStatus status;
    uint8_t copy_id;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(MD_START_DELAY_MS);

    debug_log_write("\r\nmram dump start\r\n");

    status = board_init_hardware();
    g_prefix[0] = '\0';
    log_u32("board_init_hardware", (uint32_t)status);

    for (copy_id = 1U; copy_id <= MD_COPY_COUNT; ++copy_id) {
        dump_copy(copy_id);
    }

    debug_log_write("\r\nmram dump done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
