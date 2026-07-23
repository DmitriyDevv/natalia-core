/*
 * MR25H40 MRAM bring-up test over SPI (blocking). Non-repeating pattern.
 *
 * For each bank (1 = SPI1, 2 = SPI3) it:
 *   - initializes the SPI bus and probes the chip (WREN/WRDI toggles the WEL
 *     status bit, proving the SPI link and the device respond),
 *   - shows the current first bytes of the test region,
 *   - writes a position-keyed non-repeating pattern to a small test region,
 *   - reads it back and compares byte-for-byte.
 *
 * DESTRUCTIVE for the first MRAM_TEST_SIZE bytes of each bank. MRAM has no erase
 * and no program delay, so a write is immediate.
 *
 * Requires NATALIA_ENABLE_SPI_DRIVER=ON and NATALIA_ENABLE_MRAM_DRIVER=ON.
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "mram.h"
#include "status.h"
#include "timebase.h"

#define MRAM_TEST_ADDR 0x00000U
#define MRAM_TEST_SIZE 256U

static uint8_t write_buf[MRAM_TEST_SIZE];
static uint8_t read_buf[MRAM_TEST_SIZE];

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

static uint8_t pattern_byte(uint8_t bank, uint32_t offset) {
    uint32_t x = 0x9E3779B9UL;

    x ^= (uint32_t)bank * 0x85EBCA6BUL;
    x ^= (offset + 1UL) * 0x27D4EB2FUL;

    x ^= x >> 15;
    x *= 0x2C1B3C6DUL;
    x ^= x >> 12;
    x *= 0x297A2D39UL;
    x ^= x >> 15;

    return (uint8_t)(x & 0xFFU);
}

static void check_bank(uint8_t bank_id, MramBank bank) {
    BoardStatus status;
    uint8_t is_alive = 0U;
    uint8_t status_reg = 0U;
    uint32_t mismatch = MRAM_TEST_SIZE;
    uint32_t i;

    debug_log_write("\r\n--- mram bank ");
    debug_log_write_u32_inline((uint32_t)bank_id);
    debug_log_write(" ---\r\n");

    status = mram_init(bank);
    log_kv("mram_init", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = mram_probe(bank, &is_alive);
    log_kv("probe_status", (uint32_t)status);
    log_kv("is_alive", (uint32_t)is_alive);
    if ((status != BOARD_OK) || (is_alive == 0U)) {
        return;
    }

    status = mram_read_status(bank, &status_reg);
    log_kv("read_status", (uint32_t)status);
    log_kv("status_reg", (uint32_t)status_reg);

    status = mram_read(bank, MRAM_TEST_ADDR, read_buf, 16U);
    log_kv("preread_status", (uint32_t)status);
    if (status == BOARD_OK) {
        log_hex_bytes("before", read_buf, 16U);
    }

    for (i = 0U; i < MRAM_TEST_SIZE; ++i) {
        write_buf[i] = pattern_byte(bank_id, i);
    }

    status = mram_write(bank, MRAM_TEST_ADDR, write_buf, MRAM_TEST_SIZE);
    log_kv("write_status", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    status = mram_read(bank, MRAM_TEST_ADDR, read_buf, MRAM_TEST_SIZE);
    log_kv("readback_status", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    log_hex_bytes("after", read_buf, 16U);

    for (i = 0U; i < MRAM_TEST_SIZE; ++i) {
        if (read_buf[i] != write_buf[i]) {
            mismatch = i;
            break;
        }
    }

    if (mismatch == MRAM_TEST_SIZE) {
        log_kv("verify_ok", MRAM_TEST_SIZE);
    } else {
        log_kv("first_mismatch_offset", mismatch);
        log_kv("expected", write_buf[mismatch]);
        log_kv("actual", read_buf[mismatch]);
    }
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nmram test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    check_bank(1U, MRAM_BANK_1);
    check_bank(2U, MRAM_BANK_2);

    debug_log_write("\r\nmram test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
