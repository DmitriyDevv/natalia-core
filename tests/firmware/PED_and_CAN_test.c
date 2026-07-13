#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "event_queue.h"
#include "state.h"
#include "timebase.h"
#include "transport.h"

#define STATUS_LOG_INTERVAL_MS (1000UL)

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

static void send_internal_event(SystemContext* ctx, EventType type) {
    (void)system_event_queue_push_back_type(type);
    algorithm_process_events(ctx);
}

static void collect_rtc_1hz(void) {
    uint32_t count = 0U;

    if (board_rtc_take_1hz_events(&count) != BOARD_OK) {
        return;
    }

    while (count > 0U) {
        (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
        --count;
    }
}

static void collect_ped_triggers(uint32_t* trigger_total) {
    uint32_t count = 0U;

    if (board_ped_take_trigger_events(&count) != BOARD_OK) {
        return;
    }

    while (count > 0U) {
        (void)system_event_queue_push_back_type(EVENT_PED_TRIGGER);
        ++(*trigger_total);
        --count;
    }
}

static void log_registration(const SystemContext* ctx) {
    debug_log_write("registration=");
    debug_log_write_u32_inline(ctx->observe.registration_enabled ? 1UL : 0UL);
    debug_log_write(" inhibit=");
    debug_log_write_u32_inline(ctx->ped.inhibit_enabled ? 1UL : 0UL);
    debug_log_write(" sleep=");
    debug_log_write_u32_inline(ctx->ped.sleep_enabled ? 1UL : 0UL);
    debug_log_write("\r\n");
}

int main(void) {
    SystemContext ctx;
    SystemState last_state;
    BoardStatus status;
    uint32_t now_ms;
    uint32_t last_status_ms;
    uint32_t trigger_total = 0U;
    uint32_t last_trigger_total = 0U;
    bool last_registration = false;

    status = clock_init();
    if (status != BOARD_OK) {
        while (1) {}
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        while (1) {}
    }

    (void)debug_log_init();

    debug_log_write("\r\nNATALIA PED+CAN TEST MAIN\r\n");

    init_system_context(&ctx);
    system_event_queue_init();

    send_internal_event(&ctx, EVENT_BOOT);

    status = board_comm_init();
    if (status != BOARD_OK) {
        log_status_code("board_comm_init error=", status);
        while (1) {}
    }

    send_internal_event(&ctx, EVENT_INIT_DONE);

    last_state = ctx.state;
    last_status_ms = timebase_millis();

    debug_log_write("state=");
    debug_log_write(state_to_string(ctx.state));
    debug_log_write("\r\nREADY\r\n");

    while (1) {
        now_ms = timebase_millis();

        (void)transport_poll(&ctx, now_ms);
        collect_rtc_1hz();
        collect_ped_triggers(&trigger_total);
        algorithm_poll(&ctx);
        algorithm_process_events(&ctx);

        if (ctx.state != last_state) {
            debug_log_write("STATE ");
            debug_log_write(state_to_string(last_state));
            debug_log_write(" -> ");
            debug_log_write(state_to_string(ctx.state));
            debug_log_write("\r\n");
            last_state = ctx.state;
        }

        if (ctx.observe.registration_enabled != last_registration) {
            log_registration(&ctx);
            last_registration = ctx.observe.registration_enabled;
        }

        if (timebase_elapsed(last_status_ms, STATUS_LOG_INTERVAL_MS)) {
            last_status_ms = now_ms;

            if (trigger_total != last_trigger_total) {
                debug_log_write("ped_triggers=");
                debug_log_write_u32_inline(trigger_total);
                debug_log_write("\r\n");
                last_trigger_total = trigger_total;
            }
        }
    }
}
