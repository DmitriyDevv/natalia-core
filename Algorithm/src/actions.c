#include "actions.h"
#include "alarm_monitor.h"
#include "mram_store.h"
#include "nand_map.h"
#include "observe.h"
#include "tlm_staging.h"

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

static NandRuntimeState* nand_state(SystemContext* ctx, NandBank bank) {
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

static uint16_t command_id_from_event(const SystemEvent* event) {
    if (event == NULL) {
        return 0U;
    }
    return (uint16_t)(event->msg_id & 0xFFFFU);
}

static ActionResult disconnect_nand_if_needed(SystemContext* ctx, NandBank bank) {
    NandRuntimeState* nand = nand_state(ctx, bank);

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

static ActionResult maybe_power_off_nand(SystemContext* ctx, NandBank bank, PowerAfterDone power_after_done) {
    NandRuntimeState* nand = nand_state(ctx, bank);

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

static ActionResult release_nand_bank(SystemContext* ctx, NandBank bank, PowerAfterDone power_after_done) {
    ActionResult disconnect_result = disconnect_nand_if_needed(ctx, bank);
    ActionResult power_result = maybe_power_off_nand(ctx, bank, power_after_done);

    if ((disconnect_result != ACTION_OK) || (power_result != ACTION_OK)) {
        alarm_set(ctx, ALARM_NAND_PR);
        return ACTION_ALARM;
    }

    return ACTION_OK;
}

static ActionResult disconnect_signal_line(BoardSignalTarget target) {
    return require_ok(board_disconnect_signal_lines(target));
}

static ActionResult ensure_masked_alarm_clear(const SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    if (ctx->masked_alarm != 0U) {
        return ACTION_ALARM;
    }
    return ACTION_OK;
}

static ActionResult power_off_bank(SystemContext* ctx, NandBank bank) {
    NandRuntimeState* nand = nand_state(ctx, bank);
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

static ActionResult ensure_other_bank_off(SystemContext* ctx, NandBank active_bank) {
    uint8_t is_powered = 0U;
    NandBank inactive_bank = other_bank(active_bank);
    NandRuntimeState* inactive_nand;
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

static ActionResult prepare_single_nand_bank(SystemContext* ctx, NandBank bank) {
    NandRuntimeState* nand;
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
        alarm_set(ctx, ALARM_NAND_PS);
        return ACTION_ALARM;
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

static ActionResult confirm_ped_powered(SystemContext* ctx) {
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
        alarm_set(ctx, ALARM_PED_PS);
        return ACTION_ALARM;
    }

    ctx->ped.is_powered = true;
    return ACTION_OK;
}

static void remember_first_error(ActionResult* first_error, ActionResult result) {
    if ((*first_error == ACTION_OK) && (result != ACTION_OK)) {
        *first_error = result;
    }
}

static void cleanup_failed_mode_start(SystemContext* ctx, NandBank bank, bool cleanup_ped) {
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

static uint32_t stored_packet_count(NandBank bank) {
    MramStoreServiceData service_data = {0};

    (void)mram_store_load_service_data(&service_data);

    return (bank == NAND_BANK_1) ? service_data.nand1_packet_count : service_data.nand2_packet_count;
}

static void reset_bank_progress(MramStoreServiceData* service_data, NandBank bank) {
    if (bank == NAND_BANK_1) {
        service_data->nand1_packet_count = 0U;
        service_data->nand1_last_packet_crc = 0U;
        service_data->nand1_last_dumped_packet = 0U;
    } else if (bank == NAND_BANK_2) {
        service_data->nand2_packet_count = 0U;
        service_data->nand2_last_packet_crc = 0U;
        service_data->nand2_last_dumped_packet = 0U;
    }
}

#define OBSERVE_PED_REG_TRIGGER   0x80U
#define OBSERVE_PED_REG_THRESHOLD 0x81U

static ActionResult raise_ped_start_fault(SystemContext* ctx) {
    uint32_t faults = 0U;

    (void)board_ped_take_faults(&faults);

    if ((faults & BOARD_PED_FAULT_READY) != 0U) {
        alarm_set(ctx, ALARM_PED_DIR);
    } else if ((faults & BOARD_PED_FAULT_STATUS) != 0U) {
        alarm_set(ctx, ALARM_PED_ST);
    } else {
        alarm_set(ctx, ALARM_PED_PS);
    }

    return ACTION_ALARM;
}

static ActionResult apply_observe_ped_config(SystemContext* ctx, bool power, bool sleep,
                                             bool registration, uint16_t trigger_config,
                                             uint16_t observe_params) {
    uint8_t is_powered = 0U;
    bool inhibit = sleep || !registration;
    ActionResult result;

    if (!power) {
        if (require_ok(board_ped_set_inhibit(1U)) == ACTION_OK) {
            ctx->ped.inhibit_enabled = true;
        }

        result = require_ok(board_ped_power_off());
        if (result != ACTION_OK) {
            return result;
        }

        ctx->ped.is_powered = false;
        ctx->ped.sleep_enabled = false;
        ctx->ped.status = 0U;
        return ACTION_OK;
    }

    result = require_ok(board_ped_is_powered(&is_powered));
    if (result != ACTION_OK) {
        return result;
    }

    if ((is_powered == 0U) && (board_ped_power_on() != BOARD_OK)) {
        ctx->ped.is_powered = false;
        return raise_ped_start_fault(ctx);
    }

    result = confirm_ped_powered(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    if ((board_ped_write_register(OBSERVE_PED_REG_TRIGGER, trigger_config) != BOARD_OK) ||
        (board_ped_write_register(OBSERVE_PED_REG_THRESHOLD,
                                  (uint16_t)((observe_params >> 12U) & 0x000FU)) != BOARD_OK)) {
        return raise_ped_start_fault(ctx);
    }

    result = require_ok(board_ped_set_sleep(sleep ? 1U : 0U));
    if (result != ACTION_OK) {
        return result;
    }
    ctx->ped.sleep_enabled = sleep;

    result = require_ok(board_ped_set_inhibit(inhibit ? 1U : 0U));
    if (result != ACTION_OK) {
        return result;
    }
    ctx->ped.inhibit_enabled = inhibit;

    return ACTION_OK;
}

static ActionResult release_observe_ped(SystemContext* ctx, bool power, bool sleep) {
    ActionResult result;

    result = require_ok(board_ped_set_inhibit(1U));
    if (result == ACTION_OK) {
        ctx->ped.inhibit_enabled = true;
    }

    if (!power) {
        result = require_ok(board_ped_power_off());
        if (result == ACTION_OK) {
            ctx->ped.is_powered = false;
            ctx->ped.sleep_enabled = false;
            ctx->ped.status = 0U;
        }
        return result;
    }

    if (!ctx->ped.is_powered) {
        if (board_ped_power_on() != BOARD_OK) {
            return raise_ped_start_fault(ctx);
        }

        result = confirm_ped_powered(ctx);
        if (result != ACTION_OK) {
            return result;
        }
    }

    result = require_ok(board_ped_set_sleep(sleep ? 1U : 0U));
    if (result == ACTION_OK) {
        ctx->ped.sleep_enabled = sleep;
    }

    return result;
}

ActionResult action_init_hardware(void) {
    return require_ok(board_init_hardware());
}

ActionResult action_load_mram(SystemContext* ctx) {
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

    ctx->alarm_mask = alarm_sanitize_mask(config.alarm_mask);
    ctx->alarm_status = service_data.alarm_status & ALARM_ALL_MASK;
    ctx->masked_alarm = ctx->alarm_status & ctx->alarm_mask;
    ctx->can_control = config.can_control;

    ctx->pu_temp_min = config.pu_temp_min;
    ctx->pu_temp_max = config.pu_temp_max;
    ctx->ped_temp_min = config.ped_temp_min;
    ctx->ped_temp_max = config.ped_temp_max;
    ctx->pu_voltage_min = config.pu_voltage_min;
    ctx->pu_voltage_max = config.pu_voltage_max;
    ctx->pu_current_min = config.pu_current_min;
    ctx->pu_current_max = config.pu_current_max;
    ctx->ped_voltage_min = config.ped_voltage_min;
    ctx->ped_voltage_max = config.ped_voltage_max;
    ctx->ped_current_min = config.ped_current_min;
    ctx->ped_current_max = config.ped_current_max;

    transport_apply_stored_addresses(config.device_id, config.destination_id);

    ctx->nand1.is_full = (service_data.nand1_full != 0U);
    ctx->nand2.is_full = (service_data.nand2_full != 0U);
    ctx->test.result_status = service_data.last_test_status;

    return ACTION_OK;
}

ActionResult action_check_mram(SystemContext* ctx) {
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

ActionResult action_restore_mram_copy(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return board_status_to_action(mram_store_restore_redundant_copy());
}

ActionResult action_mark_init_done(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_mark_init_fail(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->alarm_status |= ALARM_MRAM;
    return ACTION_OK;
}

ActionResult action_mark_alarm(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

ActionResult action_enter_safe_config(SystemContext* ctx) {
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
    return require_ok(transport_send_status(ctx));
}

ActionResult action_send_ack(const SystemEvent *event) {
    return action_send_ack_status(event, TRANSPORT_ACK_OK);
}

ActionResult action_send_dump_ack(const SystemContext *ctx, const SystemEvent *event) {
    uint32_t packet_count;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    packet_count = ctx->dump.size / DUMP_MODE_PACKET_SIZE;

    return require_ok(
        transport_send_dump_ack(command_id_from_event(event),
                                TRANSPORT_ACK_OK, packet_count)
    );
}

ActionResult action_send_ack_status(const SystemEvent *event,
                                    TransportAckStatus status) {
    if (event == NULL) {
        return ACTION_ERR_CONTENT;
    }

    return require_ok(
        transport_send_ack(command_id_from_event(event), status)
    );
}

ActionResult action_send_telem(const SystemContext *ctx) {
    return require_ok(transport_send_telemetry(ctx));
}

ActionResult action_send_version(void) {
    return require_ok(transport_send_version());
}

ActionResult action_set_time(const SystemEvent *event) {
    if (event == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return require_ok(board_rtc_set_time(&event->command.set_time.time));
}

ActionResult action_apply_config(SystemContext* ctx, const SystemEvent* event) {
    const CmdSetConfig* cfg;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    cfg = &event->command.set_config;

    ctx->alarm_mask = alarm_sanitize_mask(cfg->alarm_mask);
    ctx->can_control = cfg->can_control;

    ctx->pu_temp_min = cfg->pu_temp_min;
    ctx->pu_temp_max = cfg->pu_temp_max;
    ctx->ped_temp_min = cfg->ped_temp_min;
    ctx->ped_temp_max = cfg->ped_temp_max;
    ctx->pu_voltage_min = cfg->pu_voltage_min;
    ctx->pu_voltage_max = cfg->pu_voltage_max;
    ctx->pu_current_min = cfg->pu_current_min;
    ctx->pu_current_max = cfg->pu_current_max;
    ctx->ped_voltage_min = cfg->ped_voltage_min;
    ctx->ped_voltage_max = cfg->ped_voltage_max;
    ctx->ped_current_min = cfg->ped_current_min;
    ctx->ped_current_max = cfg->ped_current_max;

    if ((cfg->write_control & (1U << 0U)) != 0U) {
        ctx->observe_session_id = cfg->observe_session_id;
    }

    return ACTION_OK;
}

ActionResult action_write_mram(const SystemContext* ctx, const SystemEvent* event) {
    MramStoreConfig config = {0};
    MramStoreServiceData service_data = {0};
    const CmdSetConfig* cfg;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    cfg = &event->command.set_config;

    (void)mram_store_load_config(&config);

    config.mcu_pu_temp_min = cfg->mcu_pu_temp_min;
    config.mcu_pu_temp_max = cfg->mcu_pu_temp_max;
    config.pu_temp_min = cfg->pu_temp_min;
    config.pu_temp_max = cfg->pu_temp_max;
    config.ped_temp_min = cfg->ped_temp_min;
    config.ped_temp_max = cfg->ped_temp_max;
    config.det_temp_min = cfg->det_temp_min;
    config.det_temp_max = cfg->det_temp_max;
    config.pu_voltage_min = cfg->pu_voltage_min;
    config.pu_voltage_max = cfg->pu_voltage_max;
    config.pu_current_min = cfg->pu_current_min;
    config.pu_current_max = cfg->pu_current_max;
    config.ped_voltage_min = cfg->ped_voltage_min;
    config.ped_voltage_max = cfg->ped_voltage_max;
    config.ped_current_min = cfg->ped_current_min;
    config.ped_current_max = cfg->ped_current_max;
    config.belt_lmin = cfg->belt_lmin;
    config.belt_lmax = cfg->belt_lmax;
    config.belt_bmin = cfg->belt_bmin;
    config.ac1_rate_max = cfg->ac1_rate_max;
    config.init_rtc_time = cfg->init_rtc_time;
    config.init_rtc_time_ms = cfg->init_rtc_time_ms;
    config.can_control = cfg->can_control;
    config.alarm_mask = (uint16_t)(alarm_sanitize_mask(cfg->alarm_mask) & 0xFFFFU);

    result = board_status_to_action(mram_store_save_config(&config));
    if (result != ACTION_OK) {
        return result;
    }

    if ((cfg->write_control & 0x007FU) == 0U) {
        return ACTION_OK;
    }

    (void)mram_store_load_service_data(&service_data);

    if ((cfg->write_control & (1U << 0U)) != 0U) {
        service_data.observe_session_id = cfg->observe_session_id;
    }
    if ((cfg->write_control & (1U << 1U)) != 0U) {
        service_data.nand1_packet_count = cfg->nand1_packet_count;
    }
    if ((cfg->write_control & (1U << 2U)) != 0U) {
        service_data.nand2_packet_count = cfg->nand2_packet_count;
    }
    if ((cfg->write_control & (1U << 3U)) != 0U) {
        service_data.nand1_erase_count = cfg->nand1_erase_count;
    }
    if ((cfg->write_control & (1U << 4U)) != 0U) {
        service_data.nand2_erase_count = cfg->nand2_erase_count;
    }
    if ((cfg->write_control & (1U << 5U)) != 0U) {
        service_data.nand1_test_count = cfg->nand1_test_count;
    }
    if ((cfg->write_control & (1U << 6U)) != 0U) {
        service_data.nand2_test_count = cfg->nand2_test_count;
    }

    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_recalc_masked_alarm(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->alarm_mask = alarm_sanitize_mask(ctx->alarm_mask);
    ctx->masked_alarm = ctx->alarm_status & ctx->alarm_mask;

    return ACTION_OK;
}

ActionResult action_start_observe(SystemContext* ctx, const SystemEvent* event) {
    const CmdObserveStart* cmd;
    ObserveContext* observe;
    NandRuntimeState* nand;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    result = ensure_masked_alarm_clear(ctx);
    if (result != ACTION_OK) {
        return result;
    }

    cmd = &event->command.observe_start;
    if (!is_valid_bank(cmd->bank)) {
        return ACTION_ERR_CONTENT;
    }

    nand = nand_state(ctx, cmd->bank);
    if ((nand != NULL) && nand->is_full) {
        return ACTION_ERR_CONTENT;
    }

    observe = &ctx->observe;
    (void)memset(observe, 0, sizeof(*observe));
    observe->bank = cmd->bank;
    observe->bank_power_after_full = cmd->power_after_done;
    observe->ped_power_after_full = cmd->ped_power_after_full;
    observe->ped_sleep_after_full = cmd->ped_sleep_after_full;
    observe->ped_power_enabled = cmd->ped_power_enabled;
    observe->ped_sleep_enabled = cmd->ped_sleep_enabled;
    observe->registration_enabled = cmd->registration_enabled;
    observe->observe_params = cmd->observe_params;
    observe->trigger_config = cmd->trigger_config;
    observe->events_wait_kt = (NATALIA_OBSERVE_EVENTS_WAIT_KT != 0);
    observe->finish_target_state = STATE_DUTY;
    observe->stage = OBSERVE_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, cmd->bank);

    if (result == ACTION_OK) {
        result = require_ok(nand_map_load(bank_id(cmd->bank)));
    }

    if (result == ACTION_OK) {
        result = apply_observe_ped_config(ctx, cmd->ped_power_enabled, cmd->ped_sleep_enabled,
                                          cmd->registration_enabled, cmd->trigger_config,
                                          cmd->observe_params);
    }

    if ((result == ACTION_OK) && (observe_begin(ctx) != BOARD_OK)) {
        alarm_set(ctx, ni_writer_mram_failed(&observe->writer) ? ALARM_MRAM : ALARM_NAND_PR);
        result = ACTION_ALARM;
    }

    if (result != ACTION_OK) {
        observe->stage = OBSERVE_STAGE_EXIT_ALARM;
        cleanup_failed_mode_start(ctx, cmd->bank, true);
        return result;
    }

    return ACTION_OK;
}

ActionResult action_start_erase(SystemContext* ctx, const SystemEvent* event) {
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
    ctx->erase.operation_failed = false;
    ctx->erase.finish_requested = false;
    ctx->erase.stage = ERASE_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    result = require_ok(nand_map_load(bank_id(bank)));
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

ActionResult action_start_test(SystemContext* ctx, const SystemEvent* event) {
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
    ctx->test.packet_in_block = 0U;
    ctx->test.total_blocks = 0U;
    ctx->test.result_status = 0U;
    ctx->test.total_errors = 0U;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
    (void)memset(ctx->test.nerr, 0, sizeof(ctx->test.nerr));
    (void)memset(ctx->test.write_buffer, 0, sizeof(ctx->test.write_buffer));
    (void)memset(ctx->test.read_buffer, 0, sizeof(ctx->test.read_buffer));
    ctx->test.result_valid = false;
    ctx->test.operation_failed = false;
    ctx->test.finish_requested = false;
    ctx->test.final_erase = false;
    ctx->test.write_started = false;
    ctx->test.finish_target_state = STATE_DUTY;
    ctx->test.stage = TEST_STAGE_ENTER;

    result = prepare_single_nand_bank(ctx, bank);
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    result = require_ok(nand_map_rebuild(bank_id(bank)));
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    {
        uint32_t capacity_packets = 0U;

        result = require_ok(board_nand_get_capacity_packets(bank_id(bank), &capacity_packets));
        if (result != ACTION_OK) {
            ctx->test.stage = TEST_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return result;
        }

        ctx->test.total_blocks = capacity_packets / TEST_MODE_PACKETS_PER_BLOCK;
        if (ctx->test.total_blocks > TEST_MODE_BLOCK_COUNT) {
            ctx->test.total_blocks = TEST_MODE_BLOCK_COUNT;
        }
    }

    result = require_ok(board_nand_erase_start(bank_id(bank)));
    if (result != ACTION_OK) {
        ctx->test.result_status |= TEST_RESULT_STATUS_NAND_ERASE_ERROR;
        ctx->test.operation_failed = true;
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->test.stage = TEST_STAGE_ERASE;
    return ACTION_OK;
}

ActionResult action_start_dump(SystemContext* ctx, const SystemEvent* event) {
    NandBank bank;
    uint8_t is_ready = 0U;
    uint8_t link_present = 0U;
    uint8_t link_fault = 0U;
    uint32_t start_packet;
    uint32_t packet_count;
    uint32_t read_limit_packets;
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

    if ((event->command.dump.start_address % DUMP_MODE_PACKET_SIZE) != 0U) {
        return ACTION_ERR_CONTENT;
    }

    result = require_ok(board_data_link_present(&link_present));
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return result;
    }

    if (link_present == 0U) {
        alarm_set(ctx, ALARM_USB_VBUS);
        if (ctx->masked_alarm != 0U) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            return ACTION_ALARM;
        }
    }

    if (require_ok(board_data_link_open()) != ACTION_OK) {
        alarm_set(ctx, ALARM_USB_PR);
        (void)board_data_link_close();
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return ACTION_ALARM;
    }

    (void)board_data_link_take_fault(&link_fault);

    result = require_ok(board_data_is_ready(&is_ready));
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
    ctx->dump.tx_watch_bytes = 0U;
    ctx->dump.tx_stall_seconds = 0U;
    ctx->dump.tx_attempted = false;
    ctx->dump.link_ready = false;
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

    result = require_ok(nand_map_load(bank_id(bank)));
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    if (event->command.dump.dump_all) {
        packet_count = stored_packet_count(bank);

        if (packet_count == 0U) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return ACTION_ERR_CONTENT;
        }

        if (packet_count > (UINT32_MAX / DUMP_MODE_PACKET_SIZE)) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return ACTION_ERR_CONTENT;
        }

        ctx->dump.start_address = 0U;
        ctx->dump.size = packet_count * DUMP_MODE_PACKET_SIZE;
        start_packet = 0U;
    } else {
        if (event->command.dump.size == 0U) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return ACTION_ERR_CONTENT;
        }

        if ((event->command.dump.size % DUMP_MODE_PACKET_SIZE) != 0U) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return ACTION_ERR_CONTENT;
        }

        start_packet = event->command.dump.start_address / DUMP_MODE_PACKET_SIZE;
        packet_count = event->command.dump.size / DUMP_MODE_PACKET_SIZE;

        if (packet_count == 0U) {
            ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
            cleanup_failed_mode_start(ctx, bank, false);
            return ACTION_ERR_CONTENT;
        }
    }

    if (start_packet > (UINT32_MAX - packet_count)) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return ACTION_ERR_CONTENT;
    }

    read_limit_packets = start_packet + packet_count;

    result = require_ok(board_nand_open_read(bank_id(bank), read_limit_packets));
    if (result != ACTION_OK) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        cleanup_failed_mode_start(ctx, bank, false);
        return result;
    }

    ctx->dump.stage = DUMP_STAGE_READ;
    return ACTION_OK;
}

ActionResult action_start_shutdown(SystemContext* ctx) {
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

    (void)mram_store_load_service_data(&service_data);

    service_data.alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
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

ActionResult action_send_test_result(NandBank bank, uint8_t mram_copy) {
    static uint8_t image[BOARD_MRAM_TEST_RESULT_IMAGE_SIZE];
    uint8_t is_valid = 0U;
    BoardStatus status;

    if (!is_valid_bank(bank)) {
        return ACTION_ERR_CONTENT;
    }

    status = board_mram_read_test_result(mram_copy, bank_id(bank),
                                         image, BOARD_MRAM_TEST_RESULT_SIZE,
                                         &is_valid,
                                         &image[BOARD_MRAM_TEST_RESULT_SIZE]);
    if (status != BOARD_OK) {
        return require_ok(status);
    }

    if (is_valid == 0U) {
        return ACTION_ERR_OTHER;
    }

    return require_ok(transport_send_test_result(image,
                                                 (uint16_t)BOARD_MRAM_TEST_RESULT_IMAGE_SIZE));
}

ActionResult action_finish_erase(SystemContext* ctx, const SystemEvent* event) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->erase.finish_requested = true;
    }

    result = release_nand_bank(ctx, ctx->erase.bank, ctx->erase.power_after_done);
    if (result != ACTION_OK) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        return result;
    }

    ctx->erase.stage = ctx->erase.finish_requested ? ERASE_STAGE_FINISH_CMD : ERASE_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_erase_alarm(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->erase.bank);
}

ActionResult action_update_nand_state(SystemContext* ctx) {
    NandRuntimeState* nand;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    if (ctx->erase.operation_failed) {
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        return ACTION_ALARM;
    }

    nand = nand_state(ctx, ctx->erase.bank);
    if (nand == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand->is_full = false;
    ctx->erase.stage = ERASE_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_clear_nand_full_flag(SystemContext* ctx) {
    NandRuntimeState* nand;

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

ActionResult action_update_service_data(const SystemContext* ctx) {
    MramStoreServiceData service_data = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    (void)mram_store_load_service_data(&service_data);

    service_data.alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;
    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_update_erase_service_data(const SystemContext* ctx) {
    MramStoreServiceData service_data = {0};
    uint16_t count;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    (void)mram_store_load_service_data(&service_data);

    service_data.alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;

    reset_bank_progress(&service_data, ctx->erase.bank);
    (void)nand_map_save(bank_id(ctx->erase.bank));

    if (ctx->erase.bank == NAND_BANK_1) {
        count = service_data.nand1_erase_count;
        if (count < 0xFFFFU) {
            ++count;
        }
        service_data.nand1_erase_count = count;
    } else if (ctx->erase.bank == NAND_BANK_2) {
        count = service_data.nand2_erase_count;
        if (count < 0xFFFFU) {
            ++count;
        }
        service_data.nand2_erase_count = count;
    }

    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_update_dump_service_data(const SystemContext* ctx) {
    MramStoreServiceData service_data = {0};

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    (void)mram_store_load_service_data(&service_data);

    service_data.alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;

    if (ctx->dump.bank == NAND_BANK_1) {
        service_data.nand1_last_dumped_packet = ctx->dump.last_dumped_packet;
    } else if (ctx->dump.bank == NAND_BANK_2) {
        service_data.nand2_last_dumped_packet = ctx->dump.last_dumped_packet;
    }

    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_update_test_service_data(const SystemContext* ctx) {
    MramStoreServiceData service_data = {0};
    uint16_t count;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    (void)mram_store_load_service_data(&service_data);

    service_data.alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
    service_data.nand1_full = ctx->nand1.is_full ? 1U : 0U;
    service_data.nand2_full = ctx->nand2.is_full ? 1U : 0U;
    service_data.last_test_status = ctx->test.result_status;

    if (ctx->test.final_erase && !ctx->test.operation_failed) {
        reset_bank_progress(&service_data, ctx->test.bank);
    }
    (void)nand_map_save(bank_id(ctx->test.bank));

    if (ctx->test.bank == NAND_BANK_1) {
        count = service_data.nand1_test_count;
        if (count < 0xFFFFU) {
            ++count;
        }
        service_data.nand1_test_count = count;
    } else if (ctx->test.bank == NAND_BANK_2) {
        count = service_data.nand2_test_count;
        if (count < 0xFFFFU) {
            ++count;
        }
        service_data.nand2_test_count = count;
    }

    return board_status_to_action(mram_store_save_service_data(&service_data));
}

ActionResult action_finish_test(SystemContext* ctx, const SystemEvent* event) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->test.finish_requested = true;
        ctx->test.finish_target_state = (event->type == EVENT_CMD_SHUTDOWN) ? STATE_SHUTDOWN : STATE_DUTY;
        ctx->test.result_valid = false;
    }

    result = release_nand_bank(ctx, ctx->test.bank, ctx->test.power_after_done);
    if (result != ACTION_OK) {
        ctx->test.stage = TEST_STAGE_FINISH_ALARM;
        return result;
    }

    ctx->test.stage = (ctx->test.finish_requested || !ctx->test.result_valid)
                          ? TEST_STAGE_FINISH_CMD
                          : TEST_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_test_alarm(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->test.stage = TEST_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->test.bank);
}

ActionResult action_update_test_results(SystemContext* ctx) {
    static MramStoreTestResult result;
    ActionResult save_result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    ctx->test.stage = TEST_STAGE_SAVE;
    (void)memset(&result, 0, sizeof(result));
    result.bank = (uint8_t)ctx->test.bank;
    (void)memcpy(result.nerr, ctx->test.nerr, sizeof(result.nerr));
    save_result = board_status_to_action(mram_store_save_test_result(&result));
    if (save_result != ACTION_OK) {
        alarm_set(ctx, ALARM_MRAM);
    }
    if ((save_result == ACTION_OK) && !ctx->test.operation_failed) {
        ctx->test.result_valid = true;
    } else {
        ctx->test.result_valid = false;
        ctx->test.stage = TEST_STAGE_FINISH_CMD;
    }
    return ACTION_OK;
}

ActionResult action_observe_periodic(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    observe_on_rtc_tick(ctx);

    return ACTION_OK;
}

ActionResult action_update_observe_config(SystemContext* ctx, const SystemEvent* event) {
    const CmdObserveCtrl* cmd;
    ObserveScienceParams params;
    ActionResult result;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    if ((ctx->observe.stage != OBSERVE_STAGE_ENTER) && (ctx->observe.stage != OBSERVE_STAGE_ACTIVE)) {
        return ACTION_ERR_CONTENT;
    }

    cmd = &event->command.observe_ctrl;
    if (!observe_science_decode_params(cmd->observe_params, &params)) {
        return ACTION_ERR_CONTENT;
    }

    result = apply_observe_ped_config(ctx, cmd->ped_power_enabled, cmd->sleep_enabled,
                                      cmd->registration_enabled, cmd->trigger_config,
                                      cmd->observe_params);
    if (result != ACTION_OK) {
        return result;
    }

    ctx->observe.ped_power_enabled = cmd->ped_power_enabled;
    ctx->observe.ped_sleep_enabled = cmd->sleep_enabled;
    ctx->observe.registration_enabled = cmd->registration_enabled;
    ctx->observe.trigger_config = cmd->trigger_config;
    (void)observe_request_params(ctx, cmd->observe_params);

    return ACTION_OK;
}

ActionResult action_accept_time_sync(SystemContext* ctx, const SystemEvent* event) {
    uint8_t buffer[TLM_PAYLOAD_MAX];
    uint16_t length;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    length = tlm_staging_get(event->tlm_slot, buffer, (uint16_t)sizeof(buffer));
    observe_accept_kt(ctx, OBSERVE_KT_SYNC_ORBIT, buffer, length);

    return ACTION_OK;
}

ActionResult action_accept_mcilwain(SystemContext* ctx, const SystemEvent* event) {
    uint8_t buffer[TLM_PAYLOAD_MAX];
    uint16_t length;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    length = tlm_staging_get(event->tlm_slot, buffer, (uint16_t)sizeof(buffer));
    observe_accept_kt(ctx, OBSERVE_KT_MCILWAIN, buffer, length);

    return ACTION_OK;
}

ActionResult action_accept_magfield(SystemContext* ctx, const SystemEvent* event) {
    uint8_t buffer[TLM_PAYLOAD_MAX];
    uint16_t length;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    length = tlm_staging_get(event->tlm_slot, buffer, (uint16_t)sizeof(buffer));
    observe_accept_kt(ctx, OBSERVE_KT_GEOMAGNETIC, buffer, length);

    return ACTION_OK;
}

ActionResult action_finish_observe_full(SystemContext *ctx) {
    NandRuntimeState *nand;
    ActionResult result;
    ActionResult first_error = ACTION_OK;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    nand = nand_state(ctx, ctx->observe.bank);
    if (nand == NULL) {
        return ACTION_ERR_CONTENT;
    }

    observe_abort(ctx);

    result = release_observe_ped(ctx, ctx->observe.ped_power_after_full,
                                 ctx->observe.ped_sleep_after_full);
    remember_first_error(&first_error, result);

    result = release_nand_bank(ctx, ctx->observe.bank, ctx->observe.bank_power_after_full);
    remember_first_error(&first_error, result);

    nand->is_full = true;

    result = action_update_service_data(ctx);
    if (result != ACTION_OK) {
        alarm_set(ctx, ALARM_MRAM);
        remember_first_error(&first_error, ACTION_ALARM);
    }

    ctx->observe.stage = (first_error == ACTION_OK) ? OBSERVE_STAGE_EXIT_FULL : OBSERVE_STAGE_EXIT_ALARM;

    return first_error;
}

ActionResult action_finish_observe(SystemContext* ctx, const SystemEvent* event) {
    ObserveContext* observe;

    if ((ctx == NULL) || (event == NULL)) {
        return ACTION_ERR_CONTENT;
    }

    observe = &ctx->observe;

    if ((observe->stage == OBSERVE_STAGE_FINISHING) || (observe->stage == OBSERVE_STAGE_FLUSHING)) {
        if (event->type == EVENT_CMD_SHUTDOWN) {
            observe->finish_target_state = STATE_SHUTDOWN;
        }
        return ACTION_OK;
    }

    observe->finish_requested = true;

    if (event->type == EVENT_CMD_SHUTDOWN) {
        observe->finish_target_state = STATE_SHUTDOWN;
        observe->finish_bank_power = POWER_AFTER_DONE_OFF;
        observe->finish_ped_power = false;
        observe->finish_ped_sleep = false;
    } else {
        observe->finish_target_state = STATE_DUTY;
        observe->finish_bank_power = event->command.duty.power_after_done;
        observe->finish_ped_power = event->command.duty.ped_power_enabled;
        observe->finish_ped_sleep = event->command.duty.ped_sleep_enabled;
    }

    if (observe->stage == OBSERVE_STAGE_ENTER) {
        observe_abort(ctx);
        return action_complete_observe(ctx);
    }

    observe_request_final(ctx);

    return ACTION_OK;
}

ActionResult action_complete_observe(SystemContext* ctx) {
    ActionResult result;
    ActionResult first_error = ACTION_OK;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    result = release_observe_ped(ctx, ctx->observe.finish_ped_power, ctx->observe.finish_ped_sleep);
    remember_first_error(&first_error, result);

    result = release_nand_bank(ctx, ctx->observe.bank, ctx->observe.finish_bank_power);
    remember_first_error(&first_error, result);

    result = action_update_service_data(ctx);
    if (result != ACTION_OK) {
        alarm_set(ctx, ALARM_MRAM);
        remember_first_error(&first_error, ACTION_ALARM);
    }

    ctx->observe.stage = (first_error == ACTION_OK) ? OBSERVE_STAGE_EXIT_CMD : OBSERVE_STAGE_EXIT_ALARM;

    return first_error;
}

ActionResult action_finish_observe_alarm(SystemContext* ctx) {
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    observe_abort(ctx);
    ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;

    result = require_ok(board_ped_set_inhibit(1U));
    if (result == ACTION_OK) {
        ctx->ped.inhibit_enabled = true;
    }

    return disconnect_nand_if_needed(ctx, ctx->observe.bank);
}

ActionResult action_finish_dump(SystemContext* ctx, const SystemEvent* event) {
    size_t bytes_written = 0U;
    ActionResult flush_result;
    ActionResult result;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if ((event != NULL) && ((event->type == EVENT_CMD_DUTY) || (event->type == EVENT_CMD_SHUTDOWN))) {
        ctx->dump.finish_requested = true;
        ctx->dump.finish_target_state = (event->type == EVENT_CMD_SHUTDOWN) ? STATE_SHUTDOWN : STATE_DUTY;
    }

    flush_result = require_ok(board_data_write(NULL, 0U, &bytes_written));
    if (flush_result == ACTION_OK) {
        ctx->usb.bytes_written += (uint32_t)bytes_written;
    } else {
        alarm_set(ctx, ALARM_USB_PR);
    }

    (void)board_data_link_close();

    result = release_nand_bank(ctx, ctx->dump.bank, ctx->dump.power_after_done);
    if ((result != ACTION_OK) || (flush_result != ACTION_OK)) {
        ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
        return ACTION_ALARM;
    }

    ctx->dump.stage = ctx->dump.finish_requested ? DUMP_STAGE_FINISH_CMD : DUMP_STAGE_FINISH_OK;
    return ACTION_OK;
}

ActionResult action_finish_dump_alarm(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    (void)board_data_link_close();
    ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
    return disconnect_nand_if_needed(ctx, ctx->dump.bank);
}

ActionResult action_fix_dump_results(SystemContext* ctx) {
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

ActionResult action_clear_alarm_status(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    ctx->alarm_status = 0U;
    return ACTION_OK;
}

ActionResult action_mark_alarm_exit(SystemContext* ctx) {
    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }
    return ACTION_OK;
}

void action_set_alarm(SystemContext* ctx, uint32_t bit) {
    alarm_set(ctx, bit);
}

ActionResult action_dump_tx_watchdog(SystemContext* ctx) {
    uint32_t progress;

    if (ctx == NULL) {
        return ACTION_ERR_CONTENT;
    }

    if (ctx->dump.stage != DUMP_STAGE_SEND) {
        ctx->dump.tx_stall_seconds = 0U;
        ctx->dump.tx_attempted = false;
        return ACTION_OK;
    }

    progress = ctx->dump.bytes_done + ctx->dump.send_offset;
    if (progress != ctx->dump.tx_watch_bytes) {
        ctx->dump.tx_watch_bytes = progress;
        ctx->dump.tx_stall_seconds = 0U;
        ctx->dump.tx_attempted = false;
        return ACTION_OK;
    }

    if (!ctx->dump.tx_attempted) {
        return ACTION_OK;
    }

    ctx->dump.tx_attempted = false;
    ++ctx->dump.tx_stall_seconds;
    if (ctx->dump.tx_stall_seconds < DUMP_MODE_TX_TIMEOUT_S) {
        return ACTION_OK;
    }

    ctx->dump.operation_failed = true;
    alarm_set(ctx, ALARM_USB_PR);
    return ACTION_ALARM;
}
