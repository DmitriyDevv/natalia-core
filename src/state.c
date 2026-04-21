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

static SystemState handle_erase_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 200
        case EVENT_ERASE_DONE:
            action_update_nand_state();
            action_clear_nand_full_flag();
            action_update_service_data();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 201
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 202
        case EVENT_CMD_DUTY:
            action_send_ack();
            action_finish_erase();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 203
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_finish_erase();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        // 204
        case EVENT_CMD_TELEM_REQ:
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        // 205
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 206
        case EVENT_MASKED_ALARM_SET:
            if (ctx->masked_alarm != 0U) {
                action_finish_erase_alarm();
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_test_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 300
        case EVENT_TEST_DONE:
            action_update_test_results();
            action_update_service_data();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 301
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 302
        case EVENT_CMD_DUTY:
            action_send_ack();
            action_finish_test();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 303
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_finish_test();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        // 304
        case EVENT_CMD_TELEM_REQ:
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        // 305
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 306
        case EVENT_MASKED_ALARM_SET:
            if (ctx->masked_alarm != 0U) {
                action_finish_test_alarm();
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_observe_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 400
        case EVENT_RTC_1HZ:
            action_observe_periodic();
            return ctx->state;

        // 401
        case EVENT_PED_TRIGGER:
            action_handle_ped_trigger();
            return ctx->state;

        // 402
        case EVENT_CMD_OBSERVE_CTRL:
            action_send_ack();
            action_update_observe_config();
            return ctx->state;

        // 403
        case EVENT_CMD_TELEM_REQ:
            action_send_ack();
            action_send_telem();
            return ctx->state;

        // 404
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 405
        case EVENT_TLM_TIME_SYNC:
            action_accept_time_sync();
            return ctx->state;

        // 406
        case EVENT_TLM_ORBIT:
            action_accept_orbit();
            return ctx->state;

        // 407
        case EVENT_TLM_ATTITUDE:
            action_accept_attitude();
            return ctx->state;

        // 408
        case EVENT_TLM_MAGFIELD:
            action_accept_magfield();
            return ctx->state;

        // 409
        case EVENT_NAND_FULL:
            action_finish_observe_full();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 410
        case EVENT_CMD_DUTY:
            action_send_ack();
            action_finish_observe();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 411
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_finish_observe();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        // 412
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        // 413
        case EVENT_MASKED_ALARM_SET:
            if (ctx->masked_alarm != 0U) {
                action_finish_observe_alarm();
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_dump_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 500
        case EVENT_DUMP_DONE:
            action_fix_dump_results();
            action_update_service_data();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 501
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 502
        case EVENT_CMD_DUTY:
            action_send_ack();
            action_finish_dump();
            action_send_status();
            ctx->state = STATE_DUTY;
            return ctx->state;

        // 503
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_finish_dump();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        // 504
        case EVENT_CMD_TELEM_REQ:
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        // 505
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 506
        case EVENT_MASKED_ALARM_SET:
            if (ctx->masked_alarm != 0U) {
                action_finish_dump_alarm();
                action_enter_safe_config();
                action_send_status();
                ctx->state = STATE_ALARM;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_alarm_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 600
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 601
        case EVENT_CMD_TELEM_REQ:
            action_send_ack();
            action_send_telem();
            return ctx->state;

        // 602 / 603
        case EVENT_CMD_SET_CFG:
            action_send_ack();
            action_apply_config();
            action_write_mram();
            action_recalc_masked_alarm();

            if (ctx->masked_alarm != 0U) {
                ctx->state = STATE_ALARM;
            } else {
                ctx->state = STATE_DUTY;
            }
            return ctx->state;

        // 604 / 605
        case EVENT_CMD_RESET_ALARM:
            action_send_ack();
            action_clear_alarm_status();
            action_recalc_masked_alarm();

            if (ctx->masked_alarm != 0U) {
                ctx->state = STATE_ALARM;
            } else {
                ctx->state = STATE_DUTY;
            }
            return ctx->state;

        // 606
        case EVENT_CMD_SHUTDOWN:
            action_send_ack();
            action_start_shutdown();
            action_send_status();
            ctx->state = STATE_SHUTDOWN;
            return ctx->state;

        // 607
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUTY:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
            action_send_ack_error();
            return ctx->state;

        // 608
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 609
        case EVENT_MASKED_ALARM_CLEAR:
            if (ctx->masked_alarm == 0U) {
                action_mark_alarm_exit();
                action_send_status();
                ctx->state = STATE_DUTY;
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_shutdown_event(SystemContext *ctx, EventType event) {
    switch (event) {
        // 700
        case EVENT_CMD_STATUS_REQ:
            action_send_ack();
            action_send_status();
            return ctx->state;

        // 701
        case EVENT_CMD_TELEM_REQ:
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUTY:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_SHUTDOWN:
        case EVENT_CMD_RESET_ALARM:
            action_send_ack_error();
            return ctx->state;

        // 702
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
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
        case STATE_ERASE:
            return handle_erase_event(ctx, event);
        case STATE_TEST:
            return handle_test_event(ctx, event);
        case STATE_OBSERVE:
            return handle_observe_event(ctx, event);
        case STATE_DUMP:
            return handle_dump_event(ctx, event);
        case STATE_ALARM:
            return handle_alarm_event(ctx, event);
        case STATE_SHUTDOWN:
            return handle_shutdown_event(ctx, event);

        default: return ctx->state;
    }
}
