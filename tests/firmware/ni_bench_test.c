/*
 * Timing of the scientific-information (NI) path on real hardware.
 *
 * Part 1 - single operations, DWT cycle counter, average over NB_REPEATS calls:
 *   crc16_ccitt (Core) on 8 / 16 / 2032 bytes,
 *   a reference hardware CRC fed by 32-bit words (result compared with crc16_ccitt),
 *   memcpy of one packet (2048 bytes),
 *   ni_format_build_events for N = 1 / 10 / 100,
 *   ni_stream_append of formats until one packet is closed.
 *
 * Part 2 - the whole path for "События" formats with Nmax = 1 / 10 / 100:
 *   ni_format_build_events -> ni_stream_append -> packet,
 *   a) CPU only: ready packets are dropped;
 *   b) with NAND: ready packets go to board_nand_write_packet / board_nand_write_poll
 *      after every format (like one super-loop pass), in two variants:
 *      poll=each - one write attempt and one poll after every format;
 *      poll=lazy - write attempt when a packet is ready, otherwise a poll only if
 *                  NB_POLL_INTERVAL_US passed since the last one.
 *   While the stream has no room for the next format, the output side is polled
 *   back to back in both variants.
 *   Printed: time per packet, packets/s, events/s, and where the time went.
 *   Events are taken from a prepared array: reading the PED is not included.
 *
 * DESTRUCTIVE for bank NB_BANK_ID when NB_DO_NAND = 1 (erased before every run).
 *
 * Compile-time knobs (-DCMAKE_C_FLAGS="-D..."):
 *   NB_BANK_ID       1 or 2                              (default 1)
 *   NB_CPU_PACKETS   packets per CPU-only run            (default 2048)
 *   NB_NAND_PACKETS  packets per NAND run                (default 8192 = 16 MiB)
 *   NB_DO_NAND       0/1                                 (default 1)
 *   NB_POLL_INTERVAL_US  poll period for poll=lazy        (default 50)
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ni_bench_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "crc16.h"
#include "debug_log.h"
#include "ni_format.h"
#include "ni_stream.h"
#include "status.h"
#include "stm32l4xx.h"
#include "timebase.h"

#ifndef NB_BANK_ID
#define NB_BANK_ID 1U
#endif

#ifndef NB_CPU_PACKETS
#define NB_CPU_PACKETS 2048U
#endif

#ifndef NB_NAND_PACKETS
#define NB_NAND_PACKETS 8192U
#endif

#ifndef NB_DO_NAND
#define NB_DO_NAND 1
#endif

#ifndef NB_POLL_INTERVAL_US
#define NB_POLL_INTERVAL_US 50U
#endif

#define NB_START_DELAY_MS 5000U
#define NB_REPEATS        1000U
#define NB_SESSION_ID     1U
#define NB_FORMAT_BYTES   (NI_FORMAT_EVENTS_MAX_WORDS * 2U)

static NiStream nb_stream;
static NiEventRecord nb_events[NI_FORMAT_EVENTS_MAX];
static uint8_t nb_format[NB_FORMAT_BYTES];
static uint8_t nb_packet_a[NI_PACKET_BYTES];
static uint8_t nb_packet_b[NI_PACKET_BYTES];
static uint32_t nb_mhz;
static uint32_t nb_last_poll;
static volatile uint32_t nb_sink;

/* ------------------------------------------------------------------------- */
/* Log                                                                        */
/* ------------------------------------------------------------------------- */

static void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void log_line(const char* format, ...) {
    char line[160];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    debug_log_write(line);
}

static void halt(const char* reason) {
    log_line("STOP: %s\r\n", reason);
    for (;;) {
        __asm volatile("nop");
    }
}

static uint32_t cycles_to_ns(uint64_t cycles) {
    return (uint32_t)((cycles * 1000ULL) / nb_mhz);
}

static uint32_t cycles_to_us(uint64_t cycles) {
    return (uint32_t)(cycles / nb_mhz);
}

static void log_op(const char* name, uint64_t total_cycles, uint32_t count) {
    uint64_t average = total_cycles / count;

    log_line("  %-34s %6lu cyc  %7lu ns\r\n", name, (unsigned long)average,
             (unsigned long)cycles_to_ns(average));
}

/* ------------------------------------------------------------------------- */
/* CRC16-CCITT on the hardware unit fed by 32-bit words                       */
/* ------------------------------------------------------------------------- */

static uint16_t crc16_words(const uint8_t* data, size_t size) {
    volatile uint8_t* data_byte = (volatile uint8_t*)&CRC->DR;
    uint32_t word;

    CRC->POL = 0x1021UL;
    CRC->INIT = 0xFFFFUL;
    CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_RESET;

    while ((size > 0U) && ((((uintptr_t)data) & 3U) != 0U)) {
        *data_byte = *data;
        ++data;
        --size;
    }

    while (size >= 4U) {
        word = *(const uint32_t*)(const void*)data;
        CRC->DR = __REV(word);
        data += 4U;
        size -= 4U;
    }

    while (size > 0U) {
        *data_byte = *data;
        ++data;
        --size;
    }

    return (uint16_t)(CRC->DR & 0xFFFFUL);
}

/* ------------------------------------------------------------------------- */
/* Part 1: single operations                                                  */
/* ------------------------------------------------------------------------- */

static void fill_events(void) {
    uint32_t i;
    uint32_t x = 0x12345678UL;

    for (i = 0U; i < NI_FORMAT_EVENTS_MAX; ++i) {
        x = (x * 1664525UL) + 1013904223UL;
        nb_events[i].t_trig = (uint16_t)(x & 0xFFFFU);
        nb_events[i].t_pe_dead = (uint16_t)(x >> 16);
        nb_events[i].amp_d = (uint16_t)((x >> 8) & 0x0FFFU);
        nb_events[i].trig_stat = (uint16_t)(i & 0x00FFU);
    }

    for (i = 0U; i < NI_PACKET_BYTES; ++i) {
        x = (x * 1664525UL) + 1013904223UL;
        nb_packet_a[i] = (uint8_t)(x >> 24);
    }
}

static void bench_crc(const char* name, const uint8_t* data, size_t size, uint8_t by_words) {
    uint64_t total = 0U;
    uint32_t start;
    uint32_t i;

    for (i = 0U; i < NB_REPEATS; ++i) {
        start = timebase_cycles();
        nb_sink += by_words ? crc16_words(data, size) : crc16_ccitt(data, size);
        total += (uint32_t)(timebase_cycles() - start);
    }

    log_op(name, total, NB_REPEATS);
}

static void bench_build_events(const char* name, size_t count) {
    NiFormatStamp stamp = {1U, 0U, 0U};
    uint64_t total = 0U;
    uint32_t start;
    uint32_t i;

    for (i = 0U; i < NB_REPEATS; ++i) {
        stamp.format_number = i;
        start = timebase_cycles();
        nb_sink += (uint32_t)ni_format_build_events(&stamp, nb_events, count,
                                                    nb_format, sizeof(nb_format));
        total += (uint32_t)(timebase_cycles() - start);
    }

    log_op(name, total, NB_REPEATS);
}

/* Appends N-event formats until one packet is closed; reports the average per format
 * (the packet CRC is included once per packet). */
static void bench_append(const char* name, size_t count) {
    NiFormatStamp stamp = {1U, 0U, 0U};
    size_t words = ni_format_build_events(&stamp, nb_events, count, nb_format, sizeof(nb_format));
    uint64_t total = 0U;
    uint32_t formats = 0U;
    uint32_t packets = 0U;
    uint32_t start;

    ni_stream_begin(&nb_stream, NB_SESSION_ID, 0U, 0U);

    while (packets < 64U) {
        start = timebase_cycles();
        if (!ni_stream_append(&nb_stream, nb_format, words)) {
            halt("ni_stream_append refused");
        }
        total += (uint32_t)(timebase_cycles() - start);
        ++formats;

        while (ni_stream_ready_count(&nb_stream) > 0U) {
            ni_stream_pop(&nb_stream);
            ++packets;
        }
    }

    log_op(name, total, formats);
}

static void run_single_operations(void) {
    uint16_t crc_bytes;
    uint16_t crc_words;

    log_line("--- single operations (average of %u) ---\r\n", (unsigned)NB_REPEATS);

    bench_crc("crc16_ccitt, 8 B", nb_packet_a, 8U, 0U);
    bench_crc("crc16_ccitt, 16 B", nb_packet_a, 16U, 0U);
    bench_crc("crc16_ccitt, 2032 B (packet core)", &nb_packet_a[14], 2032U, 0U);
    bench_crc("crc16 words (bench), 16 B", nb_packet_a, 16U, 1U);
    bench_crc("crc16 words (bench), 2032 B", &nb_packet_a[14], 2032U, 1U);

    crc_bytes = crc16_ccitt(&nb_packet_a[14], 2032U);
    crc_words = crc16_words(&nb_packet_a[14], 2032U);
    log_line("  crc16 bench == crc16_ccitt: %s (0x%04X / 0x%04X)\r\n",
             (crc_bytes == crc_words) ? "yes" : "NO", (unsigned)crc_bytes, (unsigned)crc_words);

    {
        uint64_t total = 0U;
        uint32_t start;
        uint32_t i;

        for (i = 0U; i < NB_REPEATS; ++i) {
            start = timebase_cycles();
            (void)memcpy(nb_packet_b, nb_packet_a, NI_PACKET_BYTES);
            total += (uint32_t)(timebase_cycles() - start);
            nb_sink += nb_packet_b[i % NI_PACKET_BYTES];
        }
        log_op("memcpy 2048 B", total, NB_REPEATS);
    }

    bench_build_events("ni_format_build_events N=1", 1U);
    bench_build_events("ni_format_build_events N=10", 10U);
    bench_build_events("ni_format_build_events N=100", 100U);

    bench_append("ni_stream_append N=1 (14 w)", 1U);
    bench_append("ni_stream_append N=10 (50 w)", 10U);
    bench_append("ni_stream_append N=100 (410 w)", 100U);
}

/* ------------------------------------------------------------------------- */
/* Part 2: the whole path                                                     */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint64_t build;
    uint64_t append;
    uint64_t nand;
    uint64_t total;
    uint32_t formats;
    uint32_t events;
    uint32_t packets;
    uint32_t write_calls;
    uint32_t write_busy;
    uint32_t polls;
} PathStats;

#if (NB_DO_NAND != 0)
static void nand_fail(const char* stage, BoardStatus status) {
    log_line("FAIL %s status=%d\r\n", stage, (int)status);
    halt("NAND");
}

static void nand_prepare(void) {
    uint8_t is_done = 0U;
    uint32_t start_ms = timebase_millis();
    BoardStatus status;

    status = board_nand_erase_start((uint8_t)NB_BANK_ID);
    if (status != BOARD_OK) {
        nand_fail("ERASE_START", status);
    }
    while (is_done == 0U) {
        status = board_nand_erase_is_done((uint8_t)NB_BANK_ID, &is_done);
        if (status != BOARD_OK) {
            nand_fail("ERASE_POLL", status);
        }
    }
    log_line("  erase %lu ms\r\n", (unsigned long)(timebase_millis() - start_ms));

    status = board_nand_open_write((uint8_t)NB_BANK_ID, 0U);
    if (status != BOARD_OK) {
        nand_fail("OPEN_WRITE", status);
    }
}

static void nand_flush(void) {
    uint8_t is_done = 0U;
    BoardStatus status;

    while (is_done == 0U) {
        status = board_nand_write_flush((uint8_t)NB_BANK_ID, &is_done);
        if (status != BOARD_OK) {
            nand_fail("WRITE_FLUSH", status);
        }
    }
}
#endif

/* One "super-loop pass" of the output side: one write attempt, then one poll.
 * lazy != 0: without a ready packet, poll only after NB_POLL_INTERVAL_US. */
static void drain_step(PathStats* stats, uint8_t with_nand, uint8_t lazy) {
    uint32_t start = timebase_cycles();

    if ((lazy != 0U) && (ni_stream_ready_count(&nb_stream) == 0U) &&
        ((uint32_t)(start - nb_last_poll) < (NB_POLL_INTERVAL_US * nb_mhz))) {
        stats->nand += (uint32_t)(timebase_cycles() - start);
        return;
    }
    nb_last_poll = start;

    if (with_nand == 0U) {
        if (ni_stream_ready_count(&nb_stream) > 0U) {
            ni_stream_pop(&nb_stream);
            ++stats->packets;
        }
        return;
    }

#if (NB_DO_NAND != 0)
    {
        uint8_t is_idle = 0U;
        BoardStatus status;

        if (ni_stream_ready_count(&nb_stream) > 0U) {
            ++stats->write_calls;
            status = board_nand_write_packet((uint8_t)NB_BANK_ID, ni_stream_peek(&nb_stream));
            if (status == BOARD_OK) {
                ni_stream_pop(&nb_stream);
                ++stats->packets;
            } else if (status == BOARD_ERR_BUSY) {
                ++stats->write_busy;
            } else {
                nand_fail("WRITE_PACKET", status);
            }
        }

        ++stats->polls;
        status = board_nand_write_poll((uint8_t)NB_BANK_ID, &is_idle);
        if (status != BOARD_OK) {
            nand_fail("WRITE_POLL", status);
        }
    }
#endif

    stats->nand += (uint32_t)(timebase_cycles() - start);
}

static void run_path(size_t nmax, uint32_t packets, uint8_t with_nand, uint8_t lazy) {
    NiFormatStamp stamp = {1U, 0U, 0U};
    PathStats stats;
    uint32_t run_start;
    uint32_t start;
    uint32_t last;
    size_t words;
    uint32_t total_us;

    (void)memset(&stats, 0, sizeof(stats));

#if (NB_DO_NAND != 0)
    if (with_nand != 0U) {
        nand_prepare();
    }
#endif

    ni_stream_begin(&nb_stream, NB_SESSION_ID, 0U, 0U);
    run_start = timebase_cycles();
    last = run_start;

    while (stats.packets < packets) {
        start = timebase_cycles();
        words = ni_format_build_events(&stamp, nb_events, nmax, nb_format, sizeof(nb_format));
        stats.build += (uint32_t)(timebase_cycles() - start);
        ++stamp.format_number;
        ++stats.formats;
        stats.events += (uint32_t)nmax;

        while (ni_stream_free_words(&nb_stream) < words) {
            drain_step(&stats, with_nand, 0U);
        }

        start = timebase_cycles();
        if (!ni_stream_append(&nb_stream, nb_format, words)) {
            halt("ni_stream_append refused");
        }
        stats.append += (uint32_t)(timebase_cycles() - start);

        drain_step(&stats, with_nand, lazy);

        start = timebase_cycles();
        stats.total += (uint32_t)(start - last);
        last = start;
    }

#if (NB_DO_NAND != 0)
    if (with_nand != 0U) {
        nand_flush();
    }
#endif

    total_us = cycles_to_us(stats.total);
    log_line("  Nmax=%3u %s: %lu packets, %lu formats, %lu events in %lu ms\r\n",
             (unsigned)nmax,
             (with_nand == 0U) ? "CPU" : ((lazy != 0U) ? "NAND poll=lazy" : "NAND poll=each"),
             (unsigned long)stats.packets,
             (unsigned long)stats.formats, (unsigned long)stats.events,
             (unsigned long)(total_us / 1000U));
    log_line("    per packet %lu us: build %lu, append %lu, nand %lu, other %lu\r\n",
             (unsigned long)(total_us / stats.packets),
             (unsigned long)(cycles_to_us(stats.build) / stats.packets),
             (unsigned long)(cycles_to_us(stats.append) / stats.packets),
             (unsigned long)(cycles_to_us(stats.nand) / stats.packets),
             (unsigned long)(cycles_to_us(stats.total - stats.build - stats.append - stats.nand) /
                             stats.packets));
    log_line("    packets/s %lu, events/s %lu\r\n",
             (unsigned long)(((uint64_t)stats.packets * 1000000ULL) / total_us),
             (unsigned long)(((uint64_t)stats.events * 1000000ULL) / total_us));
    if (with_nand != 0U) {
        log_line("    write calls %lu (busy %lu), polls %lu\r\n", (unsigned long)stats.write_calls,
                 (unsigned long)stats.write_busy, (unsigned long)stats.polls);
    }
}

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */

int main(void) {
    static const size_t nmax_list[] = {1U, 10U, 100U};
    uint32_t i;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(NB_START_DELAY_MS);

    nb_mhz = SystemCoreClock / 1000000UL;
    log_line("\r\n===== NI BENCH TEST, core %lu MHz =====\r\n", (unsigned long)nb_mhz);

    if (board_init_hardware() != BOARD_OK) {
        halt("board_init_hardware");
    }

    fill_events();
    run_single_operations();

    log_line("--- whole path, CPU only, %u packets ---\r\n", (unsigned)NB_CPU_PACKETS);
    for (i = 0U; i < (sizeof(nmax_list) / sizeof(nmax_list[0])); ++i) {
        run_path(nmax_list[i], NB_CPU_PACKETS, 0U, 0U);
    }

#if (NB_DO_NAND != 0)
    {
        uint8_t is_powered = 0U;
        uint8_t scan_done = 0U;
        BoardStatus status;

        log_line("--- whole path with NAND bank %u, %u packets ---\r\n",
                 (unsigned)NB_BANK_ID, (unsigned)NB_NAND_PACKETS);

        status = board_nand_power_on((uint8_t)NB_BANK_ID);
        if (status == BOARD_OK) {
            status = board_nand_is_powered((uint8_t)NB_BANK_ID, &is_powered);
        }
        if ((status != BOARD_OK) || (is_powered == 0U)) {
            nand_fail("POWER_ON", status);
        }
        status = board_nand_connect((uint8_t)NB_BANK_ID);
        if (status != BOARD_OK) {
            nand_fail("CONNECT", status);
        }
        status = board_nand_bad_block_scan_start((uint8_t)NB_BANK_ID);
        while ((status == BOARD_OK) && (scan_done == 0U)) {
            status = board_nand_bad_block_scan_poll((uint8_t)NB_BANK_ID, &scan_done);
        }
        if (status != BOARD_OK) {
            nand_fail("SCAN", status);
        }

        for (i = 0U; i < (sizeof(nmax_list) / sizeof(nmax_list[0])); ++i) {
            run_path(nmax_list[i], NB_NAND_PACKETS, 1U, 0U);
            run_path(nmax_list[i], NB_NAND_PACKETS, 1U, 1U);
        }

        (void)board_nand_disconnect((uint8_t)NB_BANK_ID);
        (void)board_nand_power_off((uint8_t)NB_BANK_ID);
    }
#endif

    log_line("===== NI BENCH TEST DONE =====\r\n");
    for (;;) {
        __asm volatile("nop");
    }

    return 0;
}
