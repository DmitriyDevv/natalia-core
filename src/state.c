#include "state.h"
#include "actions.h"

static void transition_to(SystemContext *ctx, SystemState next_state) {
    if ((ctx != NULL) && (ctx->state != next_state)) {
        ctx->previous_state = ctx->state;
        ctx->state = next_state;
    }
}

static BoardAckStatus ack_status_from_result(ActionResult result) {
    switch (result) {
        case ACTION_OK:
            return BOARD_ACK_OK;
        case ACTION_ERR_CONTENT:
            return BOARD_ACK_ERR_CONTENT;
        case ACTION_ALARM:
        case ACTION_ERR_OTHER:
        default:
            return BOARD_ACK_ERR_OTHER;
    }
}

static void reject_command(const SystemEvent *event) {
    (void) action_send_ack_status(event, BOARD_ACK_ERR_MODE);
}

static bool is_alarm_active(const SystemContext *ctx) {
    return (ctx != NULL) && (ctx->masked_alarm != 0U);
}

static void enter_alarm(SystemContext *ctx) {
    (void) action_mark_alarm(ctx);
    (void) action_enter_safe_config(ctx);
    (void) action_send_status(ctx);
    transition_to(ctx, STATE_ALARM);
}

static SystemState finish_command_transition(SystemContext *ctx,
                                             const SystemEvent *event,
                                             ActionResult result,
                                             SystemState target_state) {
    if (result == ACTION_OK) {
        (void) action_send_ack(event);
        (void) action_send_status(ctx);
        transition_to(ctx, target_state);
        return ctx->state;
    }

    (void) action_send_ack_status(event, ack_status_from_result(result));
    if (result == ACTION_ALARM) {
        enter_alarm(ctx);
    }

    return ctx->state;
}

static SystemState finish_command_result(SystemContext *ctx,
                                         const SystemEvent *event,
                                         ActionResult result) {
    if (result == ACTION_OK) {
        (void) action_send_ack(event);
    } else {
        (void) action_send_ack_status(event, ack_status_from_result(result));
        if (result == ACTION_ALARM) {
            enter_alarm(ctx);
        }
    }

    return ctx->state;
}

static SystemState handle_init_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        case EVENT_BOOT:
            result = action_init_hardware();
            if (result != ACTION_OK) {
                enter_alarm(ctx);
                return ctx->state;
            }

            result = action_check_mram(ctx);
            if (result != ACTION_OK) {
                enter_alarm(ctx);
                return ctx->state;
            }

            result = action_restore_mram_copy(ctx);
            if (result != ACTION_OK) {
                enter_alarm(ctx);
                return ctx->state;
            }

            result = action_load_mram(ctx);
            if (result != ACTION_OK) {
                enter_alarm(ctx);
                return ctx->state;
            }

            return ctx->state;

        case EVENT_INIT_DONE:
            if (!is_alarm_active(ctx)) {
                (void) action_mark_init_done(ctx);
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            } else {
                enter_alarm(ctx);
            }
            return ctx->state;

        case EVENT_INIT_FAIL:
            (void) action_mark_init_fail(ctx);
            enter_alarm(ctx);
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_duty_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 100
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 101
        case EVENT_CMD_TELEM_REQ:
            (void) action_send_ack(event);
            (void) action_send_telem();
            return ctx->state;

        // 102
        case EVENT_CMD_SET_TIME:
            result = action_set_time(event);
            return finish_command_result(ctx, event, result);

        // 103 / 104
        case EVENT_CMD_SET_CFG:
            result = action_apply_config(ctx, event);
            if (result == ACTION_OK) {
                result = action_write_mram(ctx);
            }
            if (result == ACTION_OK) {
                result = action_recalc_masked_alarm(ctx);
            }
            if (result != ACTION_OK) {
                return finish_command_result(ctx, event, result);
            }

            (void) action_send_ack(event);
            if (is_alarm_active(ctx)) {
                enter_alarm(ctx);
            }
            return ctx->state;

        // 105 / 106
        case EVENT_CMD_OBSERVE_START:
            result = action_start_observe(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_OBSERVE);

        // 107
        case EVENT_CMD_ERASE:
            result = action_start_erase(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_ERASE);

        // 108
        case EVENT_CMD_TEST:
            result = action_start_test(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_TEST);

        // 109
        case EVENT_CMD_DUMP:
            result = action_start_dump(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_DUMP);

        // 110
        case EVENT_CMD_TEST_RESULT:
            result = action_send_test_result();
            return finish_command_result(ctx, event, result);

        // 111
        case EVENT_CMD_DUTY:
            (void) action_send_ack(event);
            return ctx->state;

        // 112
        case EVENT_CMD_SHUTDOWN:
            result = action_start_shutdown(ctx);
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

        // 113 / 114
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_RESET_ALARM:
            reject_command(event);
            return ctx->state;

        // 115
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 116
        case EVENT_MASKED_ALARM_SET:
            if (is_alarm_active(ctx)) {
                enter_alarm(ctx);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_erase_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 200
        case EVENT_ERASE_DONE:
            result = action_update_nand_state(ctx);
            if (result == ACTION_OK) {
                result = action_clear_nand_full_flag(ctx);
            }
            if (result == ACTION_OK) {
                result = action_update_service_data(ctx);
            }
            if (result == ACTION_OK) {
                result = action_finish_erase(ctx, event);
            }
            if (result == ACTION_OK) {
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            } else if (result == ACTION_ALARM) {
                enter_alarm(ctx);
            }
            return ctx->state;

        // 201
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 202
        case EVENT_CMD_DUTY:
            result = action_finish_erase(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_DUTY);

        // 203
        case EVENT_CMD_SHUTDOWN:
            result = action_finish_erase(ctx, event);
            if (result == ACTION_OK) {
                result = action_start_shutdown(ctx);
            }
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

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
            reject_command(event);
            return ctx->state;

        // 205
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 206
        case EVENT_MASKED_ALARM_SET:
            if (is_alarm_active(ctx)) {
                (void) action_finish_erase_alarm(ctx);
                enter_alarm(ctx);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_test_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 300
        case EVENT_TEST_DONE:
            result = action_update_test_results(ctx);
            if ((result == ACTION_OK) && ctx->test.result_valid) {
                result = action_update_service_data(ctx);
            }
            if (result == ACTION_OK) {
                result = action_finish_test(ctx, event);
            }
            if (result == ACTION_OK) {
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            } else if (result == ACTION_ALARM) {
                enter_alarm(ctx);
            }
            return ctx->state;

        // 301
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 302
        case EVENT_CMD_DUTY:
            result = action_finish_test(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_DUTY);

        // 303
        case EVENT_CMD_SHUTDOWN:
            result = action_finish_test(ctx, event);
            if (result == ACTION_OK) {
                result = action_start_shutdown(ctx);
            }
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

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
            reject_command(event);
            return ctx->state;

        // 305
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 306
        case EVENT_MASKED_ALARM_SET:
            if (is_alarm_active(ctx)) {
                (void) action_finish_test_alarm(ctx);
                enter_alarm(ctx);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_observe_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 400
        case EVENT_RTC_1HZ:
            (void) action_observe_periodic(ctx);
            return ctx->state;

        // 401
        case EVENT_PED_TRIGGER:
            (void) action_handle_ped_trigger(ctx);
            return ctx->state;

        // 402
        case EVENT_CMD_OBSERVE_CTRL:
            result = action_update_observe_config(ctx, event);
            return finish_command_result(ctx, event, result);

        // 403
        case EVENT_CMD_TELEM_REQ:
            (void) action_send_ack(event);
            (void) action_send_telem();
            return ctx->state;

        // 404
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 405
        case EVENT_TLM_TIME_SYNC:
            (void) action_accept_time_sync(ctx, event);
            return ctx->state;

        // 406
        case EVENT_TLM_ORBIT:
            (void) action_accept_orbit(ctx, event);
            return ctx->state;

        // 407
        case EVENT_TLM_ATTITUDE:
            (void) action_accept_attitude(ctx, event);
            return ctx->state;

        // 408
        case EVENT_TLM_MAGFIELD:
            (void) action_accept_magfield(ctx, event);
            return ctx->state;

        // 409
        case EVENT_NAND_FULL:
            result = action_finish_observe_full(ctx);
            if (result == ACTION_OK) {
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            } else if (result == ACTION_ALARM) {
                enter_alarm(ctx);
            }
            return ctx->state;

        // 410
        case EVENT_CMD_DUTY:
            result = action_finish_observe(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_DUTY);

        // 411
        case EVENT_CMD_SHUTDOWN:
            result = action_finish_observe(ctx, event);
            if (result == ACTION_OK) {
                result = action_start_shutdown(ctx);
            }
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

        // 412
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_SET_CFG:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
        case EVENT_CMD_RESET_ALARM:
            reject_command(event);
            return ctx->state;

        // 413
        case EVENT_MASKED_ALARM_SET:
            if (is_alarm_active(ctx)) {
                (void) action_finish_observe_alarm(ctx);
                enter_alarm(ctx);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_dump_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 500
        case EVENT_DUMP_DONE:
            result = action_fix_dump_results(ctx);
            if (result == ACTION_OK) {
                result = action_update_service_data(ctx);
            }
            if (result == ACTION_OK) {
                result = action_finish_dump(ctx, event);
            }
            if (result == ACTION_OK) {
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            } else if (result == ACTION_ALARM) {
                enter_alarm(ctx);
            }
            return ctx->state;

        // 501
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 502
        case EVENT_CMD_DUTY:
            result = action_finish_dump(ctx, event);
            return finish_command_transition(ctx, event, result, STATE_DUTY);

        // 503
        case EVENT_CMD_SHUTDOWN:
            result = action_finish_dump(ctx, event);
            if (result == ACTION_OK) {
                result = action_start_shutdown(ctx);
            }
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

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
            reject_command(event);
            return ctx->state;

        // 505
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 506
        case EVENT_MASKED_ALARM_SET:
            if (is_alarm_active(ctx)) {
                (void) action_finish_dump_alarm(ctx);
                enter_alarm(ctx);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_alarm_event(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    switch (event->type) {
        // 600
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
            return ctx->state;

        // 601
        case EVENT_CMD_TELEM_REQ:
            (void) action_send_ack(event);
            (void) action_send_telem();
            return ctx->state;

        // 602 / 603
        case EVENT_CMD_SET_CFG:
            result = action_apply_config(ctx, event);
            if (result == ACTION_OK) {
                result = action_write_mram(ctx);
            }
            if (result == ACTION_OK) {
                result = action_recalc_masked_alarm(ctx);
            }
            if (result != ACTION_OK) {
                return finish_command_result(ctx, event, result);
            }

            (void) action_send_ack(event);
            if (!is_alarm_active(ctx)) {
                transition_to(ctx, STATE_DUTY);
            }
            return ctx->state;

        // 604 / 605
        case EVENT_CMD_RESET_ALARM:
            result = action_clear_alarm_status(ctx);
            if (result == ACTION_OK) {
                result = action_recalc_masked_alarm(ctx);
            }
            if (result != ACTION_OK) {
                return finish_command_result(ctx, event, result);
            }

            (void) action_send_ack(event);
            if (!is_alarm_active(ctx)) {
                (void) action_mark_alarm_exit(ctx);
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            }
            return ctx->state;

        // 606
        case EVENT_CMD_SHUTDOWN:
            result = action_start_shutdown(ctx);
            return finish_command_transition(ctx, event, result, STATE_SHUTDOWN);

        // 607
        case EVENT_CMD_SET_TIME:
        case EVENT_CMD_OBSERVE_START:
        case EVENT_CMD_OBSERVE_CTRL:
        case EVENT_CMD_DUTY:
        case EVENT_CMD_DUMP:
        case EVENT_CMD_ERASE:
        case EVENT_CMD_TEST:
        case EVENT_CMD_TEST_RESULT:
            reject_command(event);
            return ctx->state;

        // 608
        case EVENT_TLM_TIME_SYNC:
        case EVENT_TLM_ORBIT:
        case EVENT_TLM_ATTITUDE:
        case EVENT_TLM_MAGFIELD:
            return ctx->state;

        // 609
        case EVENT_MASKED_ALARM_CLEAR:
            if (!is_alarm_active(ctx)) {
                (void) action_mark_alarm_exit(ctx);
                (void) action_send_status(ctx);
                transition_to(ctx, STATE_DUTY);
            }
            return ctx->state;

        default:
            return ctx->state;
    }
}

static SystemState handle_shutdown_event(SystemContext *ctx, const SystemEvent *event) {
    switch (event->type) {
        // 700
        case EVENT_CMD_STATUS_REQ:
            (void) action_send_ack(event);
            (void) action_send_status(ctx);
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
            reject_command(event);
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

SystemState handle_event(SystemContext *ctx, const SystemEvent *event) {
    if ((ctx == NULL) || (event == NULL)) {
        return STATE_ALARM;
    }

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
        default:
            return ctx->state;
    }
}
