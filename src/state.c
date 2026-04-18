#include "state.h"
#include "actions.h"

static SystemState handle_init_event(SystemContext *ctx, EventType event) {
    switch (event) {
        case EVENT_BOOT:
            action_init_hardware();
            action_load_mram();
            action_check_mram();
            action_restore_mram_copy();
            return ctx->state;

        case EVENT_INIT_DONE:
            if (ctx->masked_alarm == 0U) {
                action_mark_init_done();
                action_send_status();
                ctx->state = STATE_DUTY;
            } else {
                action_mark_alarm();
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        case EVENT_INIT_FAIL:
            action_mark_init_fail();
            action_enter_safe_config();
            action_send_status();
            ctx->state = STATE_ALARM;
            return ctx->state;

        default:
            return ctx->state;
    }
}


static SystemState handle_duty_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 100
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 101
        case EVENT_CMD_TELEM_REQ:
            action_send_ack();
            action_send_telem();
            return ctx->state;

        // 102
        case EVENT_CMD_SET_TIME:
            action_send_ack();
            action_set_time();
            return ctx->state;

        // 103 / 104
        case EVENT_CMD_SET_CFG:
            action_send_ack();
            action_apply_config();
            action_write_mram();
            action_recalc_masked_alarm();

            if (ctx->masked_alarm == 0U) {
                ctx->state = STATE_DUTY;
            } else {
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        /* 105 Successful path
         * TODO: 106 branch must be filtered out before the FSM or by a separate event condition
         */
        case EVENT_CMD_OBSERVE_START:
            action_send_ack();
            action_start_observe();
            action_send_status();
            ctx->state = STATE_OBSERVE;
            return ctx->state;

        // 107
        case EVENT_CMD_ERASE:
            action_send_ack();
            action_start_erase();
            action_send_status();
            ctx->state = STATE_ERASE;
            return ctx->state;

        // 108
        case EVENT_CMD_TEST:
            action_send_ack();
            action_start_test();
            action_send_status();
            ctx->state = STATE_TEST;
            return ctx->state;

        // 109
        case EVENT_CMD_DUMP:
            action_send_ack();
            action_start_dump();
            action_send_status();
            ctx->state = STATE_DUMP;
            return ctx->state;

        // 110
        case EVENT_CMD_TEST_RESULT:
            action_send_ack();
            action_send_test_result();
            return ctx->state;

        /* 111 */
        case EVENT_CMD_DUTY:
            action_send_ack();
            return ctx->state;

        /* 112 */
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_start_shutdown();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        /* 113 */
        case EVENT_CMD_OBSERVE_CTRL:
            action_send_ack_error();
            return ctx->state;

        /* 114 */
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        /* 115 */
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        /* 116 */
        case EVENT_MASKED_ALARM_SET:
            if (ctx->masked_alarm != 0U) {
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

SystemState handle_event(SystemContext *ctx, EventType event) {
    switch (ctx->state) {
        case STATE_INIT:
            return handle_init_event(ctx, event);
        case STATE_DUTY:
            return handle_duty_event(ctx, event);

        default: return ctx->state;
    }
}
