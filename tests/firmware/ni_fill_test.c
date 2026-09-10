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
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..."):
 *   NI_FILL_BANK_ID          1 or 2                       (default 1)
 *   NI_FILL_TOTAL_BYTES      total bytes to write         (default 16 MiB)
 *   NI_FILL_SESSION_ID       observation session id       (default 1)
 *   NI_FILL_DO_ERASE         0/1                          (default 1)
 *   NI_FILL_PREWARM          0/1 прогрев карты плохих блоков (default 1)
 *   NI_FILL_START_DELAY_MS   pause before touching NAND   (default 5000)
 *   NI_FILL_PROGRESS_PACKETS progress log period          (default 512)
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ni_fill_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stddef.h>
#include <stdint.h>

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
#define NI_FILL_TOTAL_BYTES (16UL * 1024UL * 1024UL)
#endif

#ifndef NI_FILL_SESSION_ID
#define NI_FILL_SESSION_ID 1U
#endif

#ifndef NI_FILL_DO_ERASE
#define NI_FILL_DO_ERASE 0
#endif

#ifndef NI_FILL_PREWARM
#define NI_FILL_PREWARM 1
#endif

#ifndef NI_FILL_START_DELAY_MS
#define NI_FILL_START_DELAY_MS 5000U
#endif

#ifndef NI_FILL_PROGRESS_PACKETS
#define NI_FILL_PROGRESS_PACKETS 512U
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

#define NI_FILL_PACKET_COUNT (NI_FILL_TOTAL_BYTES / NI_PACKET_BYTES)

#if (NI_FILL_TOTAL_BYTES % NI_PACKET_BYTES) != 0
#error "NI_FILL_TOTAL_BYTES must be a multiple of 2048"
#endif

#if (NI_FILL_PACKET_COUNT == 0)
#error "NI_FILL_TOTAL_BYTES must hold at least one packet"
#endif

static uint8_t ni_packet[NI_PACKET_BYTES];
static uint32_t ni_format_number;

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
    put_word(3U, (uint16_t)NI_FILL_SESSION_ID);
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

static void wait_write_idle(void) {
    uint8_t is_idle = 0U;
    BoardStatus status;

    while (is_idle == 0U) {
        status = board_nand_write_poll((uint8_t)NI_FILL_BANK_ID, &is_idle);
        if (status != BOARD_OK) {
            fail("WRITE_POLL", status);
        }
    }
}

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

int main(void) {
    BoardStatus status;
    uint32_t packet_index;
    uint16_t previous_crc = 0U;
    uint32_t start_ms;
    uint32_t elapsed_ms;
    uint64_t bytes;
    uint32_t bytes_per_second;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    log_text("\r\nNI FILL TEST");
    log_u32("bank", (uint32_t)NI_FILL_BANK_ID);
    log_u32("total_bytes", (uint32_t)NI_FILL_TOTAL_BYTES);
    log_u32("packets", (uint32_t)NI_FILL_PACKET_COUNT);
    log_u32("session", (uint32_t)NI_FILL_SESSION_ID);

    status = board_init_hardware();
    log_u32("board_init_hardware", (uint32_t)status);

    status = board_nand_power_on((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("POWER_ON", status);
    }

    status = board_nand_connect((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("CONNECT", status);
    }

    log_u32("start_delay_ms", (uint32_t)NI_FILL_START_DELAY_MS);
    timebase_delay_ms_blocking((uint32_t)NI_FILL_START_DELAY_MS);

    /*
     * Прогрев карты плохих блоков.
     *
     * nand_storage определяет состояние блока лениво: при первом обращении к
     * блоку он читает маркер прямо с микросхемы. Внутри цикла записи это
     * происходит в тот момент, когда предыдущая страница ещё программируется по
     * DMA, QSPI занят, чтение возвращает BOARD_ERR_BUSY, и слой ошибочно
     * помечает банк как заполненный. Отказ приходит ровно на первом пакете
     * следующего блока (пакет 64).
     *
     * Одно чтение последнего пакета заставляет слой пройти все нужные блоки и
     * заполнить карту заранее, пока запись не идёт. После стирания карта и так
     * прогрета, там этот шаг ничего не меняет.
     */
#if (NI_FILL_PREWARM != 0)
    log_text("prewarm start");

    status = board_nand_open_read((uint8_t)NI_FILL_BANK_ID,
                                  (uint32_t)NI_FILL_PACKET_COUNT);
    if (status != BOARD_OK) {
        fail("PREWARM_OPEN_READ", status);
    }

    status = board_nand_read_packet((uint8_t)NI_FILL_BANK_ID,
                                    (uint32_t)(NI_FILL_PACKET_COUNT - 1U),
                                    ni_packet);
    if (status != BOARD_OK) {
        fail("PREWARM_READ", status);
    }

    log_text("prewarm done");
#endif

#if (NI_FILL_DO_ERASE != 0)
    log_text("erase start");
    start_ms = timebase_millis();

    status = board_nand_erase_start((uint8_t)NI_FILL_BANK_ID);
    if (status != BOARD_OK) {
        fail("ERASE_START", status);
    }

    wait_erase_done();
    log_u32("erase_ms", (uint32_t)(timebase_millis() - start_ms));
#endif

    status = board_nand_open_write((uint8_t)NI_FILL_BANK_ID, 0U);
    if (status != BOARD_OK) {
        fail("OPEN_WRITE", status);
    }

    log_text("write start");
    start_ms = timebase_millis();

    for (packet_index = 0U; packet_index < NI_FILL_PACKET_COUNT; ++packet_index) {
        previous_crc = build_packet(packet_index, previous_crc);

        status = board_nand_write_packet((uint8_t)NI_FILL_BANK_ID, ni_packet);
        if (status != BOARD_OK) {
            log_u32("failed_packet", packet_index);
            fail("WRITE_PACKET", status);
        }

        wait_write_idle();

        if (((packet_index + 1U) % NI_FILL_PROGRESS_PACKETS) == 0U) {
            debug_log_write("progress packets=");
            debug_log_write_u32_inline(packet_index + 1U);
            debug_log_write(" ms=");
            debug_log_write_u32_inline((uint32_t)(timebase_millis() - start_ms));
            debug_log_write("\r\n");
        }
    }

    wait_write_flush();

    elapsed_ms = (uint32_t)(timebase_millis() - start_ms);
    bytes = (uint64_t)NI_FILL_PACKET_COUNT * (uint64_t)NI_PACKET_BYTES;

    if (elapsed_ms == 0U) {
        bytes_per_second = 0U;
    } else {
        bytes_per_second = (uint32_t)((bytes * 1000ULL) / (uint64_t)elapsed_ms);
    }

    log_text("write done");
    log_u32("packets", (uint32_t)NI_FILL_PACKET_COUNT);
    log_u32("bytes", (uint32_t)bytes);
    log_u32("ms", elapsed_ms);
    log_u32("bytes_per_second", bytes_per_second);
    log_u32("last_crc", (uint32_t)previous_crc);
    log_text("NI FILL DONE");

    halt();

    return 0;
}
