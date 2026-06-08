#include "actions.h"
#include "mram_store.h"

#include <string.h>

static ActionResult board_status_to_action(BoardStatus status) {
    switch (status) {
        case BOARD_OK:
            return ACTION_OK;
        case BOARD_ERR_INVALID_ARG:
        case BOARD_ERR_UNSUPPORTED:
            return ACTION_ERR_CONTENT;
        case BOARD_ERR_CRC:
        case BOARD_ERR_TIMEOUT:
        case BOARD_ERR_IO:
        case BOARD_ERR_BUSY:
        case BOARD_ERR_NOT_READY:
        default:
            return ACTION_ERR_OTHER;
    }
}

static bool is_valid_bank(NandBank bank) {
    return (bank == NAND_BANK_1) || (bank == NAND_BANK_2);
}

static uint8_t bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static NandRuntimeState *nand_state(SystemContext *ctx, NandBank bank) {
    if (bank == NAND_BANK_1) {
        return &ctx->nand1;
    }
    if (bank == NAND_BANK_2) {
        return &ctx->nand2;
    }
    return NULL;
}

static NandBank other_bank(NandBank bank) {
    if (bank == NAND_BANK_1) {
        return NAND_BANK_2;
    }
    if (bank == NAND_BANK_2) {
        return NAND_BANK_1;
    }
    return NAND_BANK_NONE;
}

static ActionResult apply_board_status(BoardStatus status) {
    return board_status_to_action(status);
}

static ActionResult require_ok(BoardStatus status) {
    ActionResult result = apply_board_status(status);
    return result;
}

static uint16_t command_id_from_event(const SystemEvent *event) {
    if (event == NULL) {
        return 0U;
    }
    return (uint16_t)(event->msg_id & 0xFFFFU);
}

static ActionResult disconnect_nand_if_needed(SystemContext *ctx, NandBank bank) {
    NandRuntimeState *nand = nand_state(ctx, bank);

    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    ActionResult result = require_ok(board_nand_disconnect(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    if (nand != NULL) {
        nand->is_connected = false;
    }
    return ACTION_OK;
}

static ActionResult maybe_power_off_nand(SystemContext *ctx, NandBank bank, PowerAfterDone power_after_done) {
    NandRuntimeState *nand = nand_state(ctx, bank);

    if (power_after_done == POWER_AFTER_DONE_KEEP) {
        return ACTION_OK;
    }

    ActionResult result = require_ok(board_nand_power_off(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    if (nand != NULL) {
        nand->is_powered = false;
    }
    return ACTION_OK;
}

static ActionResult disconnect_signal_line(BoardSignalTarget target) {
    return require_ok(board_disconnect_signal_lines(target));
}

static ActionResult ensure_masked_alarm_clear(const SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    if (ctx->masked_alarm != 0U) {
        return ACTION_ALARM;
    }
    return ACTION_OK;
}

static ActionResult power_off_bank(SystemContext *ctx, NandBank bank) {
    NandRuntimeState *nand = nand_state(ctx, bank);
    ActionResult result;

    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_nand_disconnect(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    result = require_ok(board_nand_power_off(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    if (nand != NULL) {
        nand->bank = bank;
        nand->is_connected = false;
        nand->is_powered = false;
    }

    return ACTION_OK;
}

static ActionResult ensure_other_bank_off(SystemContext *ctx, NandBank active_bank) {
    uint8_t is_powered = 0U;
    NandBank inactive_bank = other_bank(active_bank);
    NandRuntimeState *inactive_nand;
    ActionResult result;

    if (!is_valid_bank(inactive_bank)) {
        return ACTION_ERR_CONTENT;
    }

    inactive_nand = nand_state(ctx, inactive_bank);
    result = require_ok(board_nand_is_powered(bank_id(inactive_bank), &is_powered));
    if (result != ACTION_OK) {
        return result;
    }

    if ((is_powered != 0U) || ((inactive_nand != NULL) && inactive_nand->is_powered)) {
        return power_off_bank(ctx, inactive_bank);
    }

    return ACTION_OK;
}

static ActionResult prepare_single_nand_bank(SystemContext *ctx, NandBank bank) {
    NandRuntimeState *nand;
    uint8_t is_powered = 0U;
    ActionResult result;

    if ((ctx == NULL) || !is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_other_bank_off(ctx, bank);
    if (result != ACTION_OK) {
        return result;
    }

    nand = nand_state(ctx, bank);
    result = require_ok(board_nand_power_on(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    result = require_ok(board_nand_is_powered(bank_id(bank), &is_powered));
    if (result != ACTION_OK) {
        return result;
    }
    if (is_powered == 0U) {
        return ACTION_ERR_OTHER;
    }

    if (nand != NULL) {
        nand->bank = bank;
        nand->is_powered = true;
    }

    result = require_ok(board_nand_connect(bank_id(bank)));
    if (result != ACTION_OK) {
        return result;
    }

    if (nand != NULL) {
        nand->is_connected = true;
    }

    return ACTION_OK;
}

static ActionResult confirm_ped_powered(SystemContext *ctx) {
    uint8_t is_powered = 0U;
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_ped_is_powered(&is_powered));
    if (result != ACTION_OK) {
        return result;
    }

    if (is_powered == 0U) {
        ctx->ped.is_powered = false;
        return ACTION_ERR_OTHER;
    }

    ctx->ped.is_powered = true;
    return ACTION_OK;
}

static void remember_first_error(ActionResult *first_error, ActionResult result) {
    if ((*first_error == ACTION_OK) && (result != ACTION_OK)) {
        *first_error = result;
    }
}

static void cleanup_failed_mode_start(SystemContext *ctx, NandBank bank, bool cleanup_ped) {
    if (ctx == NULL) {
        return;
    }

    if (cleanup_ped) {
        if (require_ok(board_ped_set_inhibit(1U)) == ACTION_OK) {
            ctx->ped.inhibit_enabled = true;
        }
        if (require_ok(board_ped_power_off()) == ACTION_OK) {
            ctx->ped.is_powered = false;
        }
        ctx->observe.registration_enabled = false;
    }

    if (is_valid_bank(bank)) {
        (void)power_off_bank(ctx, bank);
    }
}

ActionResult action_init_hardware(void) {
    return require_ok(board_init_hardware());
}

ActionResult action_load_mram(SystemContext *ctx) {
    MramStoreConfig config = {0};
    MramStoreServiceData service_data = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ActionResult result = board_status_to_action(mram_store_load_config(&config));
    if (result != ACTION_OK) {
        return result;
    }

    result = board_status_to_action(mram_store_load_service_data(&service_data));
    if (result != ACTION_OK) {
        return result;
    }

    ctx->alarm_mask = config.alarm_mask;
    ctx->alarm_status = service_data.alarm_status;
    return ACTION_OK;
}

ActionResult action_check_mram(SystemContext *ctx) {
    MramStoreStatus status = {0};
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    result = board_status_to_action(mram_store_check(&status));
    if (result != ACTION_OK) {
        return result;
    }

    if (!status.copy1_valid && !status.copy2_valid) {
        ctx->alarm_status |= MRAM_STORE_ALARM_BOTH_COPIES_INVALID;
        return ACTION_ALARM;
    }

    return ACTION_OK;
}

ActionResult action_restore_mram_copy(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return board_status_to_action(mram_store_restore_redundant_copy());
}

ActionResult action_mark_init_done(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_mark_init_fail(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->alarm_status |= 1U;
    return ACTION_OK;
}

ActionResult action_mark_alarm(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_enter_safe_config(SystemContext *ctx) {
    ActionResult result;
    ActionResult first_error = ACTION_OK;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_ped_set_inhibit(1U));
    remember_first_error(&first_error, result);
    if (result == ACTION_OK) {
        ctx->ped.inhibit_enabled = true;
    }

    result = require_ok(board_ped_power_off());
    remember_first_error(&first_error, result);
    if (result == ACTION_OK) {
        ctx->ped.is_powered = false;
    }

    result = power_off_bank(ctx, NAND_BANK_1);
    remember_first_error(&first_error, result);

    result = power_off_bank(ctx, NAND_BANK_2);
    remember_first_error(&first_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_PED);
    remember_first_error(&first_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_NAND1);
    remember_first_error(&first_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_NAND2);
    remember_first_error(&first_error, result);

    result = require_ok(board_enter_safe_config());
    remember_first_error(&first_error, result);

    return first_error;
}

ActionResult action_send_status(const SystemContext *ctx) {
    (void)ctx;
    return require_ok(board_can_send_status());
}

ActionResult action_send_ack(const SystemEvent *event) {
    return action_send_ack_status(event, BOARD_ACK_OK);
}

ActionResult action_send_ack_status(const SystemEvent *event, BoardAckStatus status) {
    return require_ok(board_can_send_ack(command_id_from_event(event), status));
}

ActionResult action_send_telem(void) {
    return require_ok(board_can_send_telemetry());
}

ActionResult action_set_time(const SystemEvent *event) {
    if (event == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return require_ok(board_rtc_set_time(event->command.set_time.time_ticks));
}

ActionResult action_apply_config(SystemContext *ctx, const SystemEvent *event) {
    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    (void)event;
    return ACTION_OK;
}

ActionResult action_write_mram(const SystemContext *ctx) {
    MramStoreConfig config = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    config.alarm_mask = ctx->alarm_mask;

    return board_status_to_action(mram_store_save_config(&config));
}

ActionResult action_recalc_masked_alarm(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->masked_alarm = ctx->alarm_status & ~ctx->alarm_mask;
    return ACTION_OK;
}

ActionResult action_start_observe(SystemContext *ctx, const SystemEvent *event) {
    NandRuntimeState *nand;
    NandBank bank;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_masked_alarm_clear(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    bank = event->command.observe_start.bank;
    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    nand = nand_state(ctx, bank);
    if ((nand != NULL) && nand->is_full) {
        return ACTION_ERR_CONTENT;
    }

    ctx->observe.bank = bank;
    ctx->observe.power_after_done = event->command.observe_start.power_after_done;
    ctx->observe.acquisition_period_ticks = event->command.observe_start.acquisition_period_ticks;
    ctx->observe.events_written = 0U;
    ctx->observe.registration_enabled = false;
    ctx->observe.finish_requested = false;
    ctx->observe.finish_target_state = STATE_DUTY;
    ctx->observe.stage = OBSERVE_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, bank, true);
        return result;
    }

    result = require_ok(board_ped_power_on());
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, bank, true);
        return result;
    }

    result = confirm_ped_powered(ctx);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, bank, true);
        return result;
    }

    result = require_ok(board_ped_reg_init());
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, bank, true);
        return result;
    }

    result = require_ok(board_ped_set_inhibit(0U));
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, bank, true);
        return result;
    }
    ctx->ped.inhibit_enabled = false;
    ctx->observe.registration_enabled = true;

    ctx->observe.stage = OBSERVE_STAGE_ACTIVE;
    return ACTION_OK;
}

ActionResult action_start_erase(SystemContext *ctx, const SystemEvent *event) {
    NandBank bank;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_masked_alarm_clear(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    bank = event->command.erase.bank;
    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    ctx->erase.bank = bank;
    ctx->erase.power_after_done = event->command.erase.power_after_done;
    ctx->erase.current_address = 0U;
    ctx->erase.finish_requested = false;
    ctx->erase.stage = ERASE_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->erase.stage = ERASE_STAGE_START;
    result = require_ok(board_nand_erase_start(bank_id(bank)));
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->erase.stage = ERASE_STAGE_WAIT;
    return ACTION_OK;
}

ActionResult action_start_test(SystemContext *ctx, const SystemEvent *event) {
    NandBank bank;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_masked_alarm_clear(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    bank = event->command.test.bank;
    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    ctx->test.bank = bank;
    ctx->test.power_after_done = event->command.test.power_after_done;
    ctx->test.test_mask = event->command.test.test_mask;
    ctx->test.current_address = 0U;
    ctx->test.block_index = 0U;
    ctx->test.result_status = 0U;
    ctx->test.total_errors = 0U;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
    (void)memset(ctx->test.nerr, 0, sizeof(ctx->test.nerr));
    (void)memset(ctx->test.write_buffer, 0, sizeof(ctx->test.write_buffer));
    (void)memset(ctx->test.read_buffer, 0, sizeof(ctx->test.read_buffer));
    ctx->test.result_valid = false;
    ctx->test.operation_failed = false;
    ctx->test.finish_requested = false;
    ctx->test.finish_target_state = STATE_DUTY;
    ctx->test.stage = TEST_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->test.stage = TEST_STAGE_WRITE;
    return ACTION_OK;
}

ActionResult action_start_dump(SystemContext *ctx, const SystemEvent *event) {
    NandBank bank;
    uint8_t is_ready = 0U;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_masked_alarm_clear(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    bank = event->command.dump.bank;
    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }
    if (event->command.dump.size == 0U) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_usb_is_ready(&is_ready));
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return result;
    }
    if (is_ready == 0U) {
        ctx->usb.is_ready = false;
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return ACTION_ERR_OTHER;
    }
    ctx->usb.is_ready = true;

    ctx->dump.bank = bank;
    ctx->dump.power_after_done = event->command.dump.power_after_done;
    ctx->dump.start_address = event->command.dump.start_address;
    ctx->dump.size = event->command.dump.size;
    ctx->dump.bytes_done = 0U;
    ctx->dump.last_dumped_packet = 0U;
    ctx->dump.packet_size = 0U;
    ctx->dump.send_offset = 0U;
    ctx->dump.usb_retry_count = 0U;
    (void)memset(ctx->dump.packet_buffer, 0, sizeof(ctx->dump.packet_buffer));
    ctx->dump.operation_failed = false;
    ctx->dump.finish_requested = false;
    ctx->dump.finish_target_state = STATE_DUTY;
    ctx->dump.stage = DUMP_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->dump.stage = DUMP_STAGE_READ;
    return ACTION_OK;
}

ActionResult action_start_shutdown(SystemContext *ctx) {
    MramStoreServiceData service_data = {0};
    ActionResult result;
    ActionResult first_power_off_error = ACTION_OK;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->shutdown.source_state = ctx->state;
    ctx->shutdown.service_data_save_failed = false;
    ctx->shutdown.power_off_failed = false;
    ctx->shutdown.stage = SHUTDOWN_STAGE_STOP_ACTIVE;

    service_data.alarm_status = ctx->alarm_status;
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;

    ctx->shutdown.stage = SHUTDOWN_STAGE_SAVE_SERVICE_DATA;
    result = board_status_to_action(mram_store_save_service_data(&service_data));
    if (result != ACTION_OK) {
        ctx->shutdown.service_data_save_failed = true;
        ctx->shutdown.stage = SHUTDOWN_STAGE_ERROR;
    }

    ctx->shutdown.stage = SHUTDOWN_STAGE_POWER_OFF;
    result = require_ok(board_ped_power_off());
    remember_first_error(&first_power_off_error, result);
    if (result == ACTION_OK) {
        ctx->ped.is_powered = false;
    }

    result = power_off_bank(ctx, NAND_BANK_1);
    remember_first_error(&first_power_off_error, result);

    result = power_off_bank(ctx, NAND_BANK_2);
    remember_first_error(&first_power_off_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_PED);
    remember_first_error(&first_power_off_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_NAND1);
    remember_first_error(&first_power_off_error, result);

    result = disconnect_signal_line(BOARD_SIGNAL_NAND2);
    remember_first_error(&first_power_off_error, result);

    if (first_power_off_error != ACTION_OK) {
        ctx->shutdown.power_off_failed = true;
        ctx->shutdown.stage = SHUTDOWN_STAGE_ERROR;
        return ACTION_OK;
    }

    ctx->shutdown.stage = SHUTDOWN_STAGE_DONE;
    return ACTION_OK;
}

ActionResult action_send_test_result(void) {
    return require_ok(board_can_send_test_result());
}

ActionResult action_finish_erase(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->erase.finish_requested = true;
    }

    result = disconnect_nand_if_needed(ctx, ctx->erase.bank);
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        return result;
    }

    result = maybe_power_off_nand(ctx, ctx->erase.bank, ctx->erase.power_after_done);
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        return result;
    }

    ctx->erase.stage = ctx->erase.finish_requested ? ERASE_STAGE_FINISH_CMD : ERASE_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_erase_alarm(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->erase.bank);
}

ActionResult action_update_nand_state(SystemContext *ctx) {
    NandRuntimeState *nand;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand = nand_state(ctx, ctx->erase.bank);
    if (nand == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand->is_full = false;
    ctx->erase.stage = ERASE_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_clear_nand_full_flag(SystemContext *ctx) {
    NandRuntimeState *nand;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand = nand_state(ctx, ctx->erase.bank);
    if (nand == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand->is_full = false;
    return ACTION_OK;
}

ActionResult action_update_service_data(const SystemContext *ctx) {
    MramStoreServiceData service_data = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    service_data.alarm_status = ctx->alarm_status;
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;
    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_finish_test(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->test.finish_requested = true;
        ctx->test.finish_target_state = (event->type == EVENT_CMD_SHUTDOWN) ? STATE_SHUTDOWN : STATE_DUTY;
        ctx->test.result_valid = false;
    }

    result = disconnect_nand_if_needed(ctx, ctx->test.bank);
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        return result;
    }

    result = maybe_power_off_nand(ctx, ctx->test.bank, ctx->test.power_after_done);
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        return result;
    }

    ctx->test.stage = (ctx->test.finish_requested || !ctx->test.result_valid)
        ? TEST_STAGE_FINISH_CMD
        : TEST_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_test_alarm(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->test.stage = TEST_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->test.bank);
}

ActionResult action_update_test_results(SystemContext *ctx) {
    MramStoreTestResult result = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->test.stage = TEST_STAGE_SAVE;
    result.bank = (uint8_t)ctx->test.bank;
    result.status = ctx->test.result_status;
    result.total_errors = ctx->test.total_errors;
    result.failed_address = ctx->test.failed_address;
    (void)memcpy(result.nerr, ctx->test.nerr, sizeof(result.nerr));
    ActionResult save_result = board_status_to_action(mram_store_save_test_result(&result));
    if ((save_result == ACTION_OK) && !ctx->test.operation_failed) {
        ctx->test.result_valid = true;
    } else {
        ctx->test.result_valid = false;
        ctx->test.stage = TEST_STAGE_FINISH_CMD;
    }
    return ACTION_OK;
}

ActionResult action_observe_periodic(SystemContext *ctx) {
    uint64_t time_ticks = 0U;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    return require_ok(board_rtc_get_time(&time_ticks));
}

ActionResult action_handle_ped_trigger(SystemContext *ctx) {
    uint8_t event_buffer[16];
    size_t bytes_read = 0U;
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    if (!ctx->observe.registration_enabled) {
        return ACTION_OK;
    }

    result = require_ok(board_ped_read_event(event_buffer, sizeof(event_buffer), &bytes_read));
    if (result != ACTION_OK) {
        return result;
    }

    if (bytes_read > 0U) {
        ctx->observe.events_written++;
    }

    return require_ok(board_ped_reset_trigger());
}

ActionResult action_update_observe_config(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_ped_set_inhibit(event->command.observe_ctrl.inhibit_enabled ? 1U : 0U));
    if (result != ACTION_OK) {
        return result;
    }
    ctx->ped.inhibit_enabled = event->command.observe_ctrl.inhibit_enabled;
    ctx->observe.registration_enabled = !event->command.observe_ctrl.inhibit_enabled;

    result = require_ok(board_ped_set_sleep(event->command.observe_ctrl.sleep_enabled ? 1U : 0U));
    if (result != ACTION_OK) {
        return result;
    }
    ctx->ped.sleep_enabled = event->command.observe_ctrl.sleep_enabled;

    return ACTION_OK;
}

ActionResult action_accept_time_sync(SystemContext *ctx, const SystemEvent *event) {
    (void)event;
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_accept_orbit(SystemContext *ctx, const SystemEvent *event) {
    (void)event;
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_accept_attitude(SystemContext *ctx, const SystemEvent *event) {
    (void)event;
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_accept_magfield(SystemContext *ctx, const SystemEvent *event) {
    (void)event;
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_finish_observe_full(SystemContext *ctx) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    result = disconnect_nand_if_needed(ctx, ctx->observe.bank);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        return result;
    }
    ctx->observe.registration_enabled = false;

    result = maybe_power_off_nand(ctx, ctx->observe.bank, ctx->observe.power_after_done);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        return result;
    }

    ctx->observe.stage = OBSERVE_STAGE_EXIT_FULL;
    return ACTION_OK;
}

ActionResult action_finish_observe(SystemContext *ctx, const SystemEvent *event) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->observe.finish_requested = true;
        ctx->observe.finish_target_state = (event->type == EVENT_CMD_SHUTDOWN) ? STATE_SHUTDOWN : STATE_DUTY;
    }

    result = require_ok(board_ped_set_inhibit(1U));
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        return result;
    }
    ctx->ped.inhibit_enabled = true;
    ctx->observe.registration_enabled = false;

    result = disconnect_nand_if_needed(ctx, ctx->observe.bank);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        return result;
    }

    result = maybe_power_off_nand(ctx, ctx->observe.bank, ctx->observe.power_after_done);
    if (result != ACTION_OK) {
        ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
        return result;
    }

    ctx->observe.stage = OBSERVE_STAGE_EXIT_CMD;
    return ACTION_OK;
}

ActionResult action_finish_observe_alarm(SystemContext *ctx) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
    result = require_ok(board_ped_set_inhibit(1U));
    if (result != ACTION_OK) {
        return result;
    }
    ctx->ped.inhibit_enabled = true;
    ctx->observe.registration_enabled = false;

    return disconnect_nand_if_needed(ctx, ctx->observe.bank);
}

ActionResult action_finish_dump(SystemContext *ctx, const SystemEvent *event) {
    size_t bytes_written = 0U;
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->dump.finish_requested = true;
        ctx->dump.finish_target_state = (event->type == EVENT_CMD_SHUTDOWN) ? STATE_SHUTDOWN : STATE_DUTY;
    }

    result = require_ok(board_usb_write(NULL, 0U, &bytes_written));
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return result;
    }
    ctx->usb.bytes_written += (uint32_t)bytes_written;

    result = disconnect_nand_if_needed(ctx, ctx->dump.bank);
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return result;
    }

    result = maybe_power_off_nand(ctx, ctx->dump.bank, ctx->dump.power_after_done);
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return result;
    }

    ctx->dump.stage = ctx->dump.finish_requested ? DUMP_STAGE_FINISH_CMD : DUMP_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_dump_alarm(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->dump.bank);
}

ActionResult action_fix_dump_results(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    if (ctx->dump.operation_failed) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return ACTION_ALARM;
    }
    ctx->dump.stage = DUMP_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_clear_alarm_status(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->alarm_status = 0U;
    return ACTION_OK;
}

ActionResult action_mark_alarm_exit(SystemContext *ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}
