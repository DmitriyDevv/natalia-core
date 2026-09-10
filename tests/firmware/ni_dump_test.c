/*
 * DUMP-mode throughput test.
 *
 * Brings the real state machine up exactly like main.c (EVENT_BOOT ->
 * EVENT_INIT_DONE -> DUTY), then feeds it a single EVENT_CMD_DUMP and runs the
 * cooperative super-loop until DUMP finishes. Reading NAND and pushing bytes to
 * the host is done by the production DUMP-mode code (dump_mode_poll), not by
 * this file; the test only measures.
 *
 * The data path is board_data_write(), i.e. the FTDI/USART1 link when
 * NATALIA_ENABLE_FTDI_DRIVER=ON.
 *
 * transport_poll(), alarm_monitor_poll() and the watchdog are intentionally not
 * called: this test drives one command from inside and must not be aborted by
 * unrelated alarm sources.
 *
 * Compile-time knobs (override with -DCMAKE_C_FLAGS="-D..."):
 *   NI_DUMP_BANK_ID        1 or 2                    (default 1)
 *   NI_DUMP_START_ADDRESS  byte offset, 2048-aligned (default 0)
 *   NI_DUMP_TOTAL_BYTES    bytes to dump             (default 16 MiB)
 *   NI_DUMP_KEEP_POWER     0/1 NAND power after done (default 1)
 *   NI_DUMP_START_DELAY_MS pause before the dump starts (default 10000)
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ni_dump_test.c
 *   -DNATALIA_ENABLE_NAND_DRIVER=ON
 *   -DNATALIA_ENABLE_FTDI_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "state.h"
#include "status.h"
#include "test_mode_config.h"
#include "timebase.h"

#ifndef NI_DUMP_BANK_ID
#define NI_DUMP_BANK_ID 1
#endif

#ifndef NI_DUMP_START_ADDRESS
#define NI_DUMP_START_ADDRESS 0UL
#endif

#ifndef NI_DUMP_TOTAL_BYTES
#define NI_DUMP_TOTAL_BYTES (16UL * 1024UL * 1024UL)
#endif

#ifndef NI_DUMP_KEEP_POWER
#define NI_DUMP_KEEP_POWER 1
#endif

#ifndef NI_DUMP_START_DELAY_MS
#define NI_DUMP_START_DELAY_MS 10000U
#endif

#define NI_DUMP_FLUSH_TIMEOUT_MS 10000UL

#if (NI_DUMP_TOTAL_BYTES % DUMP_MODE_PACKET_SIZE) != 0
#error "NI_DUMP_TOTAL_BYTES must be a multiple of DUMP_MODE_PACKET_SIZE"
#endif

#if (NI_DUMP_START_ADDRESS % DUMP_MODE_PACKET_SIZE) != 0
#error "NI_DUMP_START_ADDRESS must be a multiple of DUMP_MODE_PACKET_SIZE"
#endif

static SystemContext ctx;

static const char* state_to_string(SystemState state) {
    switch (state) {
    case STATE_INIT:
        return "INIT";
    case STATE_DUTY:
        return "DUTY";
    case STATE_ERASE:
        return "ERASE";
    case STATE_TEST:
        return "TEST";
    case STATE_OBSERVE:
        return "OBSERVE";
    case STATE_DUMP:
        return "DUMP";
    case STATE_ALARM:
        return "ALARM";
    case STATE_SHUTDOWN:
        return "SHUTDOWN";
    default:
        return "UNKNOWN";
    }
}

static void log_text(const char* text) {
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void log_u32(const char* label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_state(const char* label) {
    debug_log_write(label);
    debug_log_write(" state=");
    debug_log_write(state_to_string(ctx.state));
    debug_log_write(" alarm=");
    debug_log_write_u32_inline(ctx.alarm_status);
    debug_log_write(" masked=");
    debug_log_write_u32_inline(ctx.masked_alarm);
    debug_log_write("\r\n");
}

static void halt(void) {
    while (1) {
        __asm volatile("nop");
    }
}

static void init_system_context(void) {
    (void)memset(&ctx, 0, sizeof(ctx));

    ctx.state = STATE_INIT;
    ctx.previous_state = STATE_INIT;

    ctx.alarm_status = 0U;
    ctx.alarm_mask = ALARM_ALL_MASK;
    ctx.masked_alarm = 0U;

    ctx.nand1.bank = NAND_BANK_1;
    ctx.nand2.bank = NAND_BANK_2;

    ctx.test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

static void pump_internal_event(EventType type) {
    (void)system_event_queue_push_back_type(type);
    algorithm_process_events(&ctx);
}

static void wait_output_idle(void) {
    uint32_t start = timebase_millis();
    uint8_t is_ready = 0U;

    while ((timebase_millis() - start) < NI_DUMP_FLUSH_TIMEOUT_MS) {
        if (board_data_is_ready(&is_ready) != BOARD_OK) {
            return;
        }

        if (is_ready != 0U) {
            return;
        }
    }
}

int main(void) {
    SystemEvent event;
    uint32_t start_ms;
    uint32_t send_ms;
    uint32_t total_ms;
    uint32_t bytes_written;
    uint32_t packets;
    uint64_t bits;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    log_text("\r\nNI DUMP TEST");
    log_u32("bank", (uint32_t)NI_DUMP_BANK_ID);
    log_u32("start_address", (uint32_t)NI_DUMP_START_ADDRESS);
    log_u32("total_bytes", (uint32_t)NI_DUMP_TOTAL_BYTES);
    log_u32("packet_size", (uint32_t)DUMP_MODE_PACKET_SIZE);

    init_system_context();
    system_event_queue_init();

    pump_internal_event(EVENT_BOOT);
    log_state("after BOOT");

    pump_internal_event(EVENT_INIT_DONE);
    log_state("after INIT_DONE");

    if (ctx.state != STATE_DUTY) {
        log_text("FAIL: not in DUTY");
        halt();
    }

    log_u32("start_delay_ms", (uint32_t)NI_DUMP_START_DELAY_MS);
    timebase_delay_ms_blocking((uint32_t)NI_DUMP_START_DELAY_MS);

    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.command.dump.bank =
        ((NI_DUMP_BANK_ID) == 2) ? NAND_BANK_2 : NAND_BANK_1;
#if (NI_DUMP_KEEP_POWER != 0)
    event.command.dump.power_after_done = POWER_AFTER_DONE_KEEP;
#else
    event.command.dump.power_after_done = POWER_AFTER_DONE_OFF;
#endif
    event.command.dump.start_address = (uint32_t)NI_DUMP_START_ADDRESS;
    event.command.dump.size = (uint32_t)NI_DUMP_TOTAL_BYTES;
    event.command.dump.requested_packet_count =
        (uint32_t)(NI_DUMP_TOTAL_BYTES / DUMP_MODE_PACKET_SIZE);
    event.command.dump.dump_all = false;

    log_text("EVENT_CMD_DUMP");
    (void)system_event_queue_push_back(&event);
    algorithm_process_events(&ctx);
    log_state("after CMD_DUMP");

    if (ctx.state != STATE_DUMP) {
        log_text("FAIL: dump did not start");
        log_u32("dump_stage", (uint32_t)ctx.dump.stage);
        halt();
    }

    start_ms = timebase_millis();

    while (ctx.state == STATE_DUMP) {
        algorithm_poll(&ctx);
        algorithm_process_events(&ctx);
    }

    send_ms = (uint32_t)(timebase_millis() - start_ms);

    wait_output_idle();

    total_ms = (uint32_t)(timebase_millis() - start_ms);

    bytes_written = ctx.usb.bytes_written;
    packets = ctx.dump.last_dumped_packet;

    log_state("after DUMP");
    log_u32("dump_stage", (uint32_t)ctx.dump.stage);
    log_u32("operation_failed", ctx.dump.operation_failed ? 1U : 0U);
    log_u32("packets", packets);
    log_u32("bytes", bytes_written);
    log_u32("send_ms", send_ms);
    log_u32("total_ms", total_ms);

    if (total_ms != 0U) {
        log_u32("bytes_per_second",
                (uint32_t)(((uint64_t)bytes_written * 1000ULL) / (uint64_t)total_ms));

        bits = (uint64_t)bytes_written * 8ULL * 1000ULL;
        log_u32("bits_per_second", (uint32_t)(bits / (uint64_t)total_ms));
    }

    log_text("NI DUMP DONE");

    halt();

    return 0;
}
