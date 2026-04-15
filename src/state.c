#include "state.h"

static SystemState handle_init_event(SystemContext *ctx, EventType event) {
    switch (event) {
        case EVENT_BOOT:
            return ctx->state;
        case EVENT_INIT_DONE:
            if (ctx->masked_alarm == 0U) {
                ctx->state = STATE_DUTY;
            } else {
                ctx->state = STATE_ALARM;
            }
            return ctx->state;
        case EVENT_INIT_FAIL:
            ctx->state = STATE_ALARM;
            return ctx->state;

        default:
            return ctx->state;
    }
}

SystemState handle_event(SystemContext *ctx, EventType event) {
    switch (ctx->state) {
        case STATE_INIT:
            return handle_init_event(ctx, event);

        default: return ctx->state;
    }
}
