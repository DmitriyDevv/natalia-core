/*
 * MR25H40 MRAM full-region write/verify test with per-phase timing.
 *
 * Writes a position-keyed non-repeating pattern across MRAM_FT_SIZE bytes of a
 * bank, then reads it all back and compares. The write and verify phases can be
 * toggled independently, so the same binary family can:
 *   - write + verify in one run, or
 *   - write in one run, power-cycle, then verify-only to prove MRAM retention.
 *
 * DESTRUCTIVE for the tested region. MRAM has no erase and no program delay, so
 * writes are immediate. The region is processed in chunks (no full-size RAM
 * buffer). The fill is a hash of (bank, offset): no repeating pattern, and each
 * bank holds different data.
 *
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..." or edit here):
 *   MRAM_FT_BANK_ID   0 = both banks (default), 1, or 2
 *   MRAM_FT_SIZE      bytes from 0 to test (default: whole 512 KB)
 *   MRAM_FT_DO_WRITE  0/1
 *   MRAM_FT_DO_VERIFY 0/1
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

#ifndef MRAM_FT_BANK_ID
#define MRAM_FT_BANK_ID 0
#endif

#ifndef MRAM_FT_SIZE
#define MRAM_FT_SIZE MRAM_CAPACITY_BYTES
#endif

#ifndef MRAM_FT_DO_WRITE
#define MRAM_FT_DO_WRITE 1
#endif

#ifndef MRAM_FT_DO_VERIFY
#define MRAM_FT_DO_VERIFY 1
#endif

#define MRAM_FT_CHUNK 4096U
#define MRAM_FT_PROGRESS_BYTES (65536U)

#if (MRAM_FT_SIZE < 1) || (MRAM_FT_SIZE > MRAM_CAPACITY_BYTES)
#error "MRAM_FT_SIZE out of range"
#endif

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

#if (MRAM_FT_DO_WRITE || MRAM_FT_DO_VERIFY)
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
#endif

#if MRAM_FT_DO_WRITE
static uint8_t write_buf[MRAM_FT_CHUNK];

static void phase_write(uint8_t bank_id, MramBank bank) {
    uint32_t offset = 0U;
    uint32_t next_progress = 0U;
    uint32_t fail = 0U;
    uint8_t have_first_fail = 0U;
    uint32_t t0 = timebase_millis();

    while (offset < (uint32_t)MRAM_FT_SIZE) {
        uint32_t chunk = (uint32_t)MRAM_FT_SIZE - offset;
        uint32_t i;
        BoardStatus status;

        if (chunk > MRAM_FT_CHUNK) {
            chunk = MRAM_FT_CHUNK;
        }

        if (offset >= next_progress) {
            log_kv("write_at", offset);
            next_progress += MRAM_FT_PROGRESS_BYTES;
        }

        for (i = 0U; i < chunk; ++i) {
            write_buf[i] = pattern_byte(bank_id, offset + i);
        }

        status = mram_write(bank, offset, write_buf, chunk);
        if (status != BOARD_OK) {
            ++fail;
            if (have_first_fail == 0U) {
                have_first_fail = 1U;
                log_kv("first_write_fail_offset", offset);
                log_kv("write_status", (uint32_t)status);
            }
        }

        offset += chunk;
    }

    log_kv("write_ms", timebase_millis() - t0);
    log_kv("write_fail_chunks", fail);
}
#endif

#if MRAM_FT_DO_VERIFY
static uint8_t read_buf[MRAM_FT_CHUNK];

static void phase_verify(uint8_t bank_id, MramBank bank) {
    uint32_t offset = 0U;
    uint32_t next_progress = 0U;
    uint32_t bytes_ok = 0U;
    uint32_t bytes_fail = 0U;
    uint8_t have_first_fail = 0U;
    uint32_t t0 = timebase_millis();

    while (offset < (uint32_t)MRAM_FT_SIZE) {
        uint32_t chunk = (uint32_t)MRAM_FT_SIZE - offset;
        uint32_t i;
        BoardStatus status;

        if (chunk > MRAM_FT_CHUNK) {
            chunk = MRAM_FT_CHUNK;
        }

        if (offset >= next_progress) {
            log_kv("verify_at", offset);
            next_progress += MRAM_FT_PROGRESS_BYTES;
        }

        status = mram_read(bank, offset, read_buf, chunk);
        if (status != BOARD_OK) {
            bytes_fail += chunk;
            if (have_first_fail == 0U) {
                have_first_fail = 1U;
                log_kv("first_read_fail_offset", offset);
                log_kv("read_status", (uint32_t)status);
            }
            offset += chunk;
            continue;
        }

        for (i = 0U; i < chunk; ++i) {
            if (read_buf[i] == pattern_byte(bank_id, offset + i)) {
                ++bytes_ok;
            } else {
                ++bytes_fail;
                if (have_first_fail == 0U) {
                    have_first_fail = 1U;
                    log_kv("first_mismatch_offset", offset + i);
                    log_kv("expected", pattern_byte(bank_id, offset + i));
                    log_kv("actual", read_buf[i]);
                }
            }
        }

        offset += chunk;
    }

    log_kv("verify_ms", timebase_millis() - t0);
    log_kv("bytes_ok", bytes_ok);
    log_kv("bytes_fail", bytes_fail);
}
#endif

static void check_bank(uint8_t bank_id, MramBank bank) {
    BoardStatus status;
    uint8_t is_alive = 0U;

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

    log_kv("test_size", (uint32_t)MRAM_FT_SIZE);

#if MRAM_FT_DO_WRITE
    phase_write(bank_id, bank);
#endif
#if MRAM_FT_DO_VERIFY
    phase_verify(bank_id, bank);
#endif
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nmram full test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    log_kv("do_write", (uint32_t)MRAM_FT_DO_WRITE);
    log_kv("do_verify", (uint32_t)MRAM_FT_DO_VERIFY);

#if (MRAM_FT_BANK_ID == 0) || (MRAM_FT_BANK_ID == 1)
    check_bank(1U, MRAM_BANK_1);
#endif
#if (MRAM_FT_BANK_ID == 0) || (MRAM_FT_BANK_ID == 2)
    check_bank(2U, MRAM_BANK_2);
#endif

    debug_log_write("\r\nmram full test done\r\n");

    while (1) {
        __asm volatile("nop");
    }
}
