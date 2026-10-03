/*
 * NAND fill with scientific-information (NI) packets.
 *
 * Writes NI packets into one NAND bank so that a following DUMP run has real,
 * verifiable data to transmit. Packet structure follows
 * "Формат научной информации ГС v6", section 2:
 *
 *   word 1..3     markers 46FFh, C9D7h, A5B3h
 *   word 4        observation session id
 *   word 5..6     packet number (low, high)
 *   word 7        CRC16 of the previous packet (0 in packet 0)
 *   word 8..1023  information core
 *   word 1024     CRC16 of the information core of this packet
 *
 * 16-bit words are stored little-endian, one packet is 1024 words = 2048 bytes.
 * The core is filled with "Счетчики" (format 01h) records, 15 words each;
 * the tail that does not fit a whole record is padded with AAAAh as required
 * by section 2.3.
 *
 * DESTRUCTIVE: the selected bank is erased before writing.
 *
 * Writing goes through the Board_API / nand_storage double buffer: on BUSY the
 * storage is polled and the packet is offered again, so the active + queued
 * write path of nand_storage is exercised exactly as in flight.
 *
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..."):
 *   NI_FILL_BANK_ID          1 or 2                                    (default 1)
 *   NI_FILL_TOTAL_BYTES      total bytes; 0 = whole bank capacity      (default 0)
 *   NI_FILL_SESSION_ID       observation session id of cycle 0         (default 1)
 *   NI_FILL_DO_ERASE         0/1                                       (default 1)
 *   NI_FILL_DO_WRITE         0/1                                       (default 1)
 *   NI_FILL_DO_VERIFY        0/1 read back and byte-exact compare      (default 1)
 *   NI_FILL_SPLIT_PACKET     0 = one write session; N = write [0, N), switch the
 *                            bank off and on again, append [N, end)    (default 0)
 *   NI_FILL_CYCLES           erase/write/verify cycles; cycle k writes session
 *                            NI_FILL_SESSION_ID + k, so every cycle has new data (default 1)
 *   NI_FILL_PREWARM          0/1 bad-block map prewarm read            (default 1)
 *   NI_FILL_START_DELAY_MS   pause before touching NAND                (default 5000)
 *   NI_FILL_PROGRESS_PACKETS progress log period                       (default 16384)
 *
 * Retention check after a full board power-off: run once with defaults, then flash
 * the same build with NI_FILL_DO_ERASE=0, NI_FILL_DO_WRITE=0, power the board off,
 * power it on again and read the verify result (session NI_FILL_SESSION_ID).
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ni_fill_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "crc16.h"
#include "debug_log.h"
#include "status.h"
#include "timebase.h"

#ifndef NI_FILL_BANK_ID
#define NI_FILL_BANK_ID 1
#endif

#ifndef NI_FILL_TOTAL_BYTES
#define NI_FILL_TOTAL_BYTES 0UL
#endif

#ifndef NI_FILL_SESSION_ID
#define NI_FILL_SESSION_ID 1U
#endif

#ifndef NI_FILL_DO_ERASE
#define NI_FILL_DO_ERASE 1
#endif

#ifndef NI_FILL_PREWARM
#define NI_FILL_PREWARM 1
#endif

#ifndef NI_FILL_DO_WRITE
#define NI_FILL_DO_WRITE 1
#endif

#ifndef NI_FILL_DO_VERIFY
#define NI_FILL_DO_VERIFY 1
#endif

#ifndef NI_FILL_SPLIT_PACKET
#define NI_FILL_SPLIT_PACKET 0UL
#endif

#ifndef NI_FILL_CYCLES
#define NI_FILL_CYCLES 1U
#endif

#ifndef NI_FILL_VERIFY_LOG_MISMATCHES
#define NI_FILL_VERIFY_LOG_MISMATCHES 8U
#endif

#ifndef NI_FILL_START_DELAY_MS
#define NI_FILL_START_DELAY_MS 5000U
#endif

#ifndef NI_FILL_PROGRESS_PACKETS
#define NI_FILL_PROGRESS_PACKETS 16384U
#endif

#define NI_PACKET_WORDS    1024U
#define NI_PACKET_BYTES    (NI_PACKET_WORDS * 2U)
#define NI_HEADER_WORDS    7U
#define NI_CORE_WORDS      (NI_PACKET_WORDS - NI_HEADER_WORDS - 1U)

#define NI_MARKER_1        0x46FFU
#define NI_MARKER_2        0xC9D7U
#define NI_MARKER_3        0xA5B3U
#define NI_PAD_WORD        0xAAAAU

#define NI_FORMAT_MARK_1   0xFEFAU
#define NI_FORMAT_MARK_2   0x000FU
#define NI_FORMAT_COUNTERS 0x01U
#define NI_OBSERVE_MODE    0x01U
#define NI_COUNTERS_WORDS  15U

#if (NI_FILL_TOTAL_BYTES % NI_PACKET_BYTES) != 0
#error "NI_FILL_TOTAL_BYTES must be a multiple of 2048"
#endif

#if (NI_FILL_CYCLES < 1)
#error "NI_FILL_CYCLES must be at least 1"
#endif

static uint8_t ni_packet[NI_PACKET_BYTES];
static uint8_t ni_readback[NI_PACKET_BYTES];
static uint32_t ni_format_number;
static uint16_t ni_session_id;
static uint32_t ni_packet_count;

static void log_text(const char *text) {
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void log_u32(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void halt(void) {
    while (1) {
        __asm volatile("nop");
    }
}

static void fail(const char *stage, BoardStatus status) {
    debug_log_write("FAIL ");
    debug_log_write(stage);
    debug_log_write(" status=");
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");
    halt();
}

static void put_word(uint32_t word_index, uint16_t value) {
    ni_packet[word_index * 2U] = (uint8_t)(value & 0xFFU);
    ni_packet[(word_index * 2U) + 1U] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t mix16(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352DUL;
    value ^= value >> 15;
    value *= 0x846CA68BUL;
    value ^= value >> 16;

    return (uint16_t)(value & 0xFFFFU);
}

/* Одна запись формата "Счетчики" (01h), 15 слов. */
static uint32_t build_counters_record(uint32_t word_index, uint32_t rtc_time) {
    uint16_t crc;

    put_word(word_index + 0U, NI_FORMAT_MARK_1);
    put_word(word_index + 1U, NI_FORMAT_MARK_2);
    put_word(word_index + 2U,
             (uint16_t)(((uint16_t)NI_OBSERVE_MODE << 8) | NI_FORMAT_COUNTERS));
    put_word(word_index + 3U, (uint16_t)(ni_format_number & 0xFFFFU));
    put_word(word_index + 4U, (uint16_t)((ni_format_number >> 16) & 0xFFFFU));
    put_word(word_index + 5U, (uint16_t)(rtc_time & 0xFFFFU));
    put_word(word_index + 6U, (uint16_t)((rtc_time >> 16) & 0xFFFFU));
    put_word(word_index + 7U, (uint16_t)NI_COUNTERS_WORDS);

    crc = crc16_ccitt(&ni_packet[word_index * 2U], 8U * 2U);
    put_word(word_index + 8U, crc);

    put_word(word_index + 9U,  mix16(ni_format_number + 0x1111U));
    put_word(word_index + 10U, mix16(ni_format_number + 0x2222U));
    put_word(word_index + 11U, mix16(ni_format_number + 0x3333U));
    put_word(word_index + 12U, mix16(ni_format_number + 0x4444U));
    put_word(word_index + 13U, mix16(ni_format_number + 0x5555U));

    crc = crc16_ccitt(&ni_packet[(word_index + 9U) * 2U], 5U * 2U);
    put_word(word_index + 14U, crc);

    ++ni_format_number;

    return word_index + NI_COUNTERS_WORDS;
}

/* Возвращает CRC16 ядра — она же слово 1024 и "CRC предыдущего" для следующего. */
static uint16_t build_packet(uint32_t packet_index, uint16_t previous_crc) {
    uint32_t word_index;
    uint32_t core_end;
    uint16_t crc;

    put_word(0U, NI_MARKER_1);
    put_word(1U, NI_MARKER_2);
    put_word(2U, NI_MARKER_3);
    put_word(3U, ni_session_id);
    put_word(4U, (uint16_t)(packet_index & 0xFFFFU));
    put_word(5U, (uint16_t)((packet_index >> 16) & 0xFFFFU));
    put_word(6U, previous_crc);

    word_index = NI_HEADER_WORDS;
    core_end = NI_HEADER_WORDS + NI_CORE_WORDS;

    while ((word_index + NI_COUNTERS_WORDS) <= core_end) {
        word_index = build_counters_record(word_index, packet_index);
    }

    while (word_index < core_end) {
        put_word(word_index, NI_PAD_WORD);
        ++word_index;
    }

    crc = crc16_ccitt(&ni_packet[NI_HEADER_WORDS * 2U], NI_CORE_WORDS * 2U);
    put_word(NI_PACKET_WORDS - 1U, crc);

    return crc;
}

#if (NI_FILL_DO_ERASE != 0)
static void wait_erase_done(void) {
    uint8_t is_done = 0U;
    BoardStatus status;

    while (is_done == 0U) {
        status = board_nand_erase_is_done((uint8_t)NI_FILL_BANK_ID, &is_done);
        if (status != BOARD_OK) {
            fail("ERASE_POLL", status);
        }
    }
}

#endif

#if (NI_FILL_DO_WRITE != 0)
static void wait_write_flush(void) {
    uint8_t is_done = 0U;
    BoardStatus status;

    while (is_done == 0U) {
        status = board_nand_write_flush((uint8_t)NI_FILL_BANK_ID, &is_done);
        if (status != BOARD_OK) {
            fail("WRITE_FLUSH", status);
        }
    }
}

#endif

static void bank_up(void) {
    uint8_t is_powered = 0U;
    BoardStatus status;

    status = board_nand_power_on((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("POWER_ON", status);
    }

    status = board_nand_is_powered((uint8_t)NI_FILL_BANK_ID, &is_powered);
    if ((status != BOARD_OK) || (is_powered == 0U)) {
        fail("NOT_POWERED", status);
    }

    status = board_nand_connect((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("CONNECT", status);
    }
}

static void scan_bad_blocks(void) {
    uint8_t scan_done = 0U;
    BoardStatus status;

    status = board_nand_bad_block_scan_start((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("SCAN_START", status);
    }

    while (scan_done == 0U) {
        status = board_nand_bad_block_scan_poll((uint8_t)NI_FILL_BANK_ID, &scan_done);
        if (status != BOARD_OK) {
            fail("SCAN_POLL", status);
        }
    }
}

#if (NI_FILL_DO_WRITE != 0)
static void bank_power_cycle(void) {
    static BoardNandBlockMap map;
    BoardStatus status;

    status = board_nand_get_block_map((uint8_t)NI_FILL_BANK_ID, &map);
    if (status != BOARD_OK) {
        fail("GET_MAP", status);
    }

    status = board_nand_disconnect((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("DISCONNECT", status);
    }

    status = board_nand_power_off((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("POWER_OFF", status);
    }

    log_text("bank power off");
    timebase_delay_ms_blocking(500U);

    bank_up();
    log_text("bank power on");

    status = board_nand_set_block_map((uint8_t)NI_FILL_BANK_ID, &map);
    if (status != BOARD_OK) {
        fail("SET_MAP", status);
    }
}

/* Packet content depends on everything before it (CRC chain, format numbers): replay up to first. */
static uint16_t chain_state_at(uint32_t first) {
    uint32_t packet_index;
    uint16_t previous_crc = 0U;

    ni_format_number = 0U;

    for (packet_index = 0U; packet_index < first; ++packet_index) {
        previous_crc = build_packet(packet_index, previous_crc);
    }

    return previous_crc;
}

static void write_range(uint32_t first, uint32_t end) {
    uint32_t packet_index;
    uint16_t previous_crc;
    uint8_t is_idle = 0U;
    BoardStatus status;

    previous_crc = chain_state_at(first);

    status = board_nand_open_write((uint8_t)NI_FILL_BANK_ID, first);
    if (status != BOARD_OK) {
        fail("OPEN_WRITE", status);
    }

    for (packet_index = first; packet_index < end; ++packet_index) {
        previous_crc = build_packet(packet_index, previous_crc);

        for (;;) {
            status = board_nand_write_packet((uint8_t)NI_FILL_BANK_ID, ni_packet);
            if (status != BOARD_ERR_BUSY) {
                break;
            }

            status = board_nand_write_poll((uint8_t)NI_FILL_BANK_ID, &is_idle);
            if (status != BOARD_OK) {
                log_u32("failed_packet", packet_index);
                fail("WRITE_POLL", status);
            }
        }

        if (status != BOARD_OK) {
            log_u32("failed_packet", packet_index);
            fail("WRITE_PACKET", status);
        }

        if (((packet_index + 1U) % NI_FILL_PROGRESS_PACKETS) == 0U) {
            log_u32("write_packets", packet_index + 1U);
        }
    }

    wait_write_flush();
}

#endif

#if (NI_FILL_DO_VERIFY != 0)
static uint32_t first_diff_offset(void) {
    uint32_t offset;

    for (offset = 0U; offset < NI_PACKET_BYTES; ++offset) {
        if (ni_packet[offset] != ni_readback[offset]) {
            return offset;
        }
    }

    return NI_PACKET_BYTES;
}

/* Returns the number of bad packets (mismatch or uncorrectable ECC). */
static uint32_t verify_bank(void) {
    uint32_t packet_index;
    uint16_t previous_crc = 0U;
    uint32_t bad_packets = 0U;
    uint32_t ecc_packets = 0U;
    uint32_t start_ms;
    BoardStatus status;

    status = board_nand_open_read((uint8_t)NI_FILL_BANK_ID, ni_packet_count);
    if (status != BOARD_OK) {
        fail("VERIFY_OPEN_READ", status);
    }

    start_ms = timebase_millis();
    ni_format_number = 0U;

    for (packet_index = 0U; packet_index < ni_packet_count; ++packet_index) {
        previous_crc = build_packet(packet_index, previous_crc);

        status = board_nand_read_packet((uint8_t)NI_FILL_BANK_ID, packet_index, ni_readback);
        if (status == BOARD_ERR_CRC) {
            ++ecc_packets;
        } else if (status != BOARD_OK) {
            log_u32("failed_packet", packet_index);
            fail("VERIFY_READ", status);
        }

        if (memcmp(ni_packet, ni_readback, NI_PACKET_BYTES) != 0) {
            if (bad_packets < NI_FILL_VERIFY_LOG_MISMATCHES) {
                debug_log_write("mismatch packet=");
                debug_log_write_u32_inline(packet_index);
                debug_log_write(" offset=");
                debug_log_write_u32_inline(first_diff_offset());
                debug_log_write("\r\n");
            }
            ++bad_packets;
        }

        if (((packet_index + 1U) % NI_FILL_PROGRESS_PACKETS) == 0U) {
            debug_log_write("verify_packets=");
            debug_log_write_u32_inline(packet_index + 1U);
            debug_log_write(" bad=");
            debug_log_write_u32_inline(bad_packets);
            debug_log_write("\r\n");
        }
    }

    log_u32("verify_ms", (uint32_t)(timebase_millis() - start_ms));
    log_u32("verify_bad_packets", bad_packets);
    log_u32("verify_ecc_uncorrectable", ecc_packets);

    return bad_packets + ecc_packets;
}

#endif

int main(void) {
    BoardStatus status;
    uint32_t capacity = 0U;
    uint32_t cycle;
#if ((NI_FILL_DO_ERASE != 0) || (NI_FILL_DO_WRITE != 0))
    uint32_t start_ms;
#endif
    uint32_t failed_cycles = 0U;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    log_u32("start_delay_ms", (uint32_t)NI_FILL_START_DELAY_MS);
    timebase_delay_ms_blocking((uint32_t)NI_FILL_START_DELAY_MS);

    log_text("\r\nNI FILL TEST");
    log_u32("bank", (uint32_t)NI_FILL_BANK_ID);
    log_u32("do_erase", (uint32_t)NI_FILL_DO_ERASE);
    log_u32("do_write", (uint32_t)NI_FILL_DO_WRITE);
    log_u32("do_verify", (uint32_t)NI_FILL_DO_VERIFY);
    log_u32("split_packet", (uint32_t)NI_FILL_SPLIT_PACKET);
    log_u32("cycles", (uint32_t)NI_FILL_CYCLES);
    log_u32("session", (uint32_t)NI_FILL_SESSION_ID);

    status = board_init_hardware();
    log_u32("board_init_hardware", (uint32_t)status);

    bank_up();
    scan_bad_blocks();

    status = board_nand_get_capacity_packets((uint8_t)NI_FILL_BANK_ID, &capacity);
    if (status != BOARD_OK) {
        fail("CAPACITY", status);
    }

    ni_packet_count = (NI_FILL_TOTAL_BYTES == 0UL) ? capacity
                                                   : (uint32_t)(NI_FILL_TOTAL_BYTES / NI_PACKET_BYTES);
    log_u32("capacity_packets", capacity);
    log_u32("packets", ni_packet_count);
    if ((ni_packet_count == 0U) || (ni_packet_count > capacity) ||
        ((uint32_t)NI_FILL_SPLIT_PACKET >= ni_packet_count)) {
        fail("PACKET_COUNT", BOARD_ERR_INVALID_ARG);
    }

#if (NI_FILL_PREWARM != 0)
    status = board_nand_open_read((uint8_t)NI_FILL_BANK_ID, ni_packet_count);
    if (status == BOARD_OK) {
        status = board_nand_read_packet((uint8_t)NI_FILL_BANK_ID, ni_packet_count - 1U, ni_readback);
    }
    if ((status != BOARD_OK) && (status != BOARD_ERR_CRC)) {
        fail("PREWARM", status);
    }
#endif

    for (cycle = 0U; cycle < (uint32_t)NI_FILL_CYCLES; ++cycle) {
        uint32_t bad = 0U;

        ni_session_id = (uint16_t)((uint32_t)NI_FILL_SESSION_ID + cycle);
        log_u32("cycle", cycle);
        log_u32("cycle_session", (uint32_t)ni_session_id);

#if (NI_FILL_DO_ERASE != 0)
        start_ms = timebase_millis();
        status = board_nand_erase_start((uint8_t)NI_FILL_BANK_ID);
        if (status != BOARD_OK) {
            fail("ERASE_START", status);
        }
        wait_erase_done();
        log_u32("erase_ms", (uint32_t)(timebase_millis() - start_ms));
#endif

#if (NI_FILL_DO_WRITE != 0)
        start_ms = timebase_millis();
        if ((uint32_t)NI_FILL_SPLIT_PACKET != 0U) {
            write_range(0U, (uint32_t)NI_FILL_SPLIT_PACKET);
            log_u32("session_1_packets", (uint32_t)NI_FILL_SPLIT_PACKET);
            bank_power_cycle();
            write_range((uint32_t)NI_FILL_SPLIT_PACKET, ni_packet_count);
        } else {
            write_range(0U, ni_packet_count);
        }
        log_u32("write_ms", (uint32_t)(timebase_millis() - start_ms));
#endif

#if (NI_FILL_DO_VERIFY != 0)
        bad = verify_bank();
#endif

        if (bad != 0U) {
            ++failed_cycles;
            log_text("CYCLE FAILED");
        } else {
            log_text("CYCLE OK");
        }
    }

    log_u32("failed_cycles", failed_cycles);
    log_text((failed_cycles == 0U) ? "NI FILL: ALL OK" : "NI FILL: FAILED");

    halt();

    return 0;
}
