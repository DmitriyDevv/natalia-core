#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "can1.h"
#include "clock.h"
#include "debug_log.h"
#include "state.h"
#include "timebase.h"
#include "transport.h"
#include "unican.h"

#define MAIN_STAGE_LOG_INTERVAL_MS (1000UL)

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

static void log_status_code(const char* prefix, BoardStatus status) {
    debug_log_write(prefix);
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");
}

static void log_context_line(const SystemContext* ctx) {
    debug_log_write("state=");
    debug_log_write(state_to_string(ctx->state));
    debug_log_write(" prev=");
    debug_log_write(state_to_string(ctx->previous_state));
    debug_log_write(" alarm=");
    debug_log_write_u32_inline(ctx->alarm_status);
    debug_log_write(" masked=");
    debug_log_write_u32_inline(ctx->masked_alarm);
    debug_log_write("\r\n");
}

static void log_state_change(const SystemContext* ctx,
                             SystemState* last_state) {
    if (ctx->state == *last_state) {
        return;
    }

    debug_log_write("STATE ");
    debug_log_write(state_to_string(*last_state));
    debug_log_write(" -> ");
    debug_log_write(state_to_string(ctx->state));
    debug_log_write("\r\n");

    log_context_line(ctx);

    *last_state = ctx->state;
}

static void log_mode_stage(const SystemContext* ctx) {
    switch (ctx->state) {
    case STATE_ERASE:
        debug_log_write("erase_stage=");
        debug_log_write_u32_inline((uint32_t)ctx->erase.stage);
        debug_log_write(" bank=");
        debug_log_write_u32_inline((uint32_t)ctx->erase.bank);
        debug_log_write("\r\n");
        break;

    case STATE_TEST:
        debug_log_write("test_stage=");
        debug_log_write_u32_inline((uint32_t)ctx->test.stage);
        debug_log_write(" bank=");
        debug_log_write_u32_inline((uint32_t)ctx->test.bank);
        debug_log_write(" block=");
        debug_log_write_u32_inline(ctx->test.block_index);
        debug_log_write(" errors=");
        debug_log_write_u32_inline(ctx->test.total_errors);
        debug_log_write("\r\n");
        break;

    case STATE_DUMP:
        debug_log_write("dump_stage=");
        debug_log_write_u32_inline((uint32_t)ctx->dump.stage);
        debug_log_write(" bank=");
        debug_log_write_u32_inline((uint32_t)ctx->dump.bank);
        debug_log_write(" bytes=");
        debug_log_write_u32_inline(ctx->dump.bytes_done);
        debug_log_write(" size=");
        debug_log_write_u32_inline(ctx->dump.size);
        debug_log_write("\r\n");
        break;

    case STATE_OBSERVE:
        debug_log_write("observe_stage=");
        debug_log_write_u32_inline((uint32_t)ctx->observe.stage);
        debug_log_write(" bank=");
        debug_log_write_u32_inline((uint32_t)ctx->observe.bank);
        debug_log_write(" packets=");
        debug_log_write_u32_inline(ctx->observe.committed_packet_count);
        debug_log_write("\r\n");
        break;

    default:
        break;
    }
}

static void send_internal_event(SystemContext* ctx, EventType type) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = 0U;

    (void)handle_event(ctx, &event);
}

static void init_system_context(SystemContext* ctx) {
    (void)memset(ctx, 0, sizeof(*ctx));

    ctx->state = STATE_INIT;
    ctx->previous_state = STATE_INIT;

    ctx->alarm_status = 0U;
    ctx->alarm_mask = ALARM_ALL_MASK;
    ctx->masked_alarm = 0U;

    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;

    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

static void log_can_stats_periodic(uint32_t* last_log_ms) {
    uint32_t now_ms;
    Can1Stats stats;

    now_ms = timebase_millis();

    if (!timebase_elapsed(*last_log_ms, 5000UL)) {
        return;
    }

    *last_log_ms = now_ms;

    can1_get_stats(&stats);

    debug_log_write("can rx=");
    debug_log_write_u32_inline(stats.rx_received);
    debug_log_write(" drop=");
    debug_log_write_u32_inline(stats.rx_dropped);
    debug_log_write(" txq=");
    debug_log_write_u32_inline(stats.tx_queued);
    debug_log_write(" txc=");
    debug_log_write_u32_inline(stats.tx_completed);
    debug_log_write(" txf=");
    debug_log_write_u32_inline(stats.tx_failed);
    debug_log_write(" boff=");
    debug_log_write_u32_inline(stats.bus_off_events);
    debug_log_write("\r\n");
}

static void log_unican_stats_periodic(uint32_t* last_log_ms) {
    uint32_t now_ms;
    UnicanStatus status;

    now_ms = timebase_millis();

    if (!timebase_elapsed(*last_log_ms, 5000UL)) {
        return;
    }

    *last_log_ms = now_ms;

    unican_get_status(&status);

    debug_log_write("unican online=");
    debug_log_write_u32_inline(status.is_online ? 1UL : 0UL);
    debug_log_write(" tx_busy=");
    debug_log_write_u32_inline(status.tx_busy ? 1UL : 0UL);
    debug_log_write(" rx_ok=");
    debug_log_write_u32_inline(status.rx_messages_ok);
    debug_log_write(" tx_ok=");
    debug_log_write_u32_inline(status.tx_messages_ok);
    debug_log_write(" tx_fail=");
    debug_log_write_u32_inline(status.tx_messages_failed);
    debug_log_write(" drop=");
    debug_log_write_u32_inline(status.dropped_messages);
    debug_log_write("\r\n");
}

int main(void) {
    SystemContext ctx;
    SystemState last_state;
    BoardStatus status;
    BoardStatus last_transport_status;
    uint32_t now_ms;
    uint32_t last_stage_log_ms;
    uint32_t last_can_log_ms;
    uint32_t last_unican_log_ms;

    status = clock_init();
    if (status != BOARD_OK) {
        while (1) {}
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        while (1) {}
    }

    (void)debug_log_init();

    debug_log_write("\r\nNATALIA CAN TEST MAIN\r\n");
    debug_log_write("sysclk=");
    debug_log_write_u32_inline(clock_get_sysclk_hz());
    debug_log_write(" hclk=");
    debug_log_write_u32_inline(clock_get_hclk_hz());
    debug_log_write(" pclk1=");
    debug_log_write_u32_inline(clock_get_pclk1_hz());
    debug_log_write(" pclk2=");
    debug_log_write_u32_inline(clock_get_pclk2_hz());
    debug_log_write("\r\n");

    init_system_context(&ctx);

    debug_log_write("EVENT_BOOT\r\n");
    send_internal_event(&ctx, EVENT_BOOT);
    log_context_line(&ctx);

    status = can1_init();
    if (status != BOARD_OK) {
        log_status_code("can1_init error=", status);
        while (1) {}
    }

    unican_init();

    debug_log_write("EVENT_INIT_DONE\r\n");
    send_internal_event(&ctx, EVENT_INIT_DONE);
    log_context_line(&ctx);

    last_state = ctx.state;
    last_transport_status = BOARD_OK;
    last_stage_log_ms = timebase_millis();
    last_can_log_ms = timebase_millis();
    last_unican_log_ms = timebase_millis();

    debug_log_write("READY\r\n");

    while (1) {
        now_ms = timebase_millis();

        status = transport_poll(&ctx, now_ms);
        if (status != last_transport_status) {
            log_status_code("transport_status=", status);
            last_transport_status = status;
        }

        algorithm_poll(&ctx);

        log_state_change(&ctx, &last_state);

        if (timebase_elapsed(last_stage_log_ms, MAIN_STAGE_LOG_INTERVAL_MS)) {
            last_stage_log_ms = now_ms;
            log_mode_stage(&ctx);
        }

        log_can_stats_periodic(&last_can_log_ms);
        log_unican_stats_periodic(&last_unican_log_ms);
    }
}
