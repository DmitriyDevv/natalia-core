#include "actions.h"
#include "board_api.h"

static const uint8_t default_nand_bank = 1U;
static const uint16_t unknown_command_id = 0U;

static void ignore_board_status(BoardStatus status) {
    (void)status;
}

void action_init_hardware(void) {
    ignore_board_status(board_init_hardware());
}

void action_load_mram(void) {
}

void action_check_mram(void) {
}

void action_restore_mram_copy(void) {
}

void action_mark_init_done(void) {
}

void action_mark_init_fail(void) {
}

void action_mark_alarm(void) {
}

void action_enter_safe_config(void) {
    ignore_board_status(board_ped_power_off());
    ignore_board_status(board_nand_power_off(1U));
    ignore_board_status(board_nand_power_off(2U));
    ignore_board_status(board_enter_safe_config());
}

void action_send_status(void) {
    ignore_board_status(board_can_send_status());
}

void action_send_ack(void) {
    ignore_board_status(board_can_send_ack(unknown_command_id, BOARD_ACK_OK));
}

void action_send_ack_error(void) {
    ignore_board_status(board_can_send_ack(unknown_command_id, BOARD_ACK_ERR_MODE));
}

void action_send_telem(void) {
    ignore_board_status(board_can_send_telemetry());
}

void action_set_time(void) {
}

void action_apply_config(void) {
}

void action_write_mram(void) {
}

void action_recalc_masked_alarm(void) {
}

void action_start_observe(void) {
    ignore_board_status(board_nand_power_on(default_nand_bank));
    ignore_board_status(board_nand_connect(default_nand_bank));
    ignore_board_status(board_ped_power_on());
    ignore_board_status(board_ped_reg_init());
}

void action_start_erase(void) {
    ignore_board_status(board_nand_power_on(default_nand_bank));
    ignore_board_status(board_nand_connect(default_nand_bank));
    ignore_board_status(board_nand_erase_start(default_nand_bank));
}

void action_start_test(void) {
    ignore_board_status(board_nand_power_on(default_nand_bank));
    ignore_board_status(board_nand_connect(default_nand_bank));
}

void action_start_dump(void) {
    uint8_t is_ready = 0U;

    ignore_board_status(board_usb_is_ready(&is_ready));
}

void action_start_shutdown(void) {
    ignore_board_status(board_ped_power_off());
    ignore_board_status(board_nand_power_off(1U));
    ignore_board_status(board_nand_power_off(2U));
    ignore_board_status(board_disconnect_signal_lines(BOARD_SIGNAL_PED));
    ignore_board_status(board_disconnect_signal_lines(BOARD_SIGNAL_NAND1));
    ignore_board_status(board_disconnect_signal_lines(BOARD_SIGNAL_NAND2));
}

void action_send_test_result(void) {
    ignore_board_status(board_can_send_test_result());
}

void action_finish_erase(void) {
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_finish_erase_alarm(void) {
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_update_nand_state(void) {
}

void action_clear_nand_full_flag(void) {
}

void action_update_service_data(void) {
}

void action_finish_test(void) {
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_finish_test_alarm(void) {
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_update_test_results(void) {
}

void action_observe_periodic(void) {
    uint64_t time_ticks = 0U;

    ignore_board_status(board_rtc_get_time(&time_ticks));
}

void action_handle_ped_trigger(void) {
    uint8_t event_buffer[16];
    size_t bytes_read = 0U;

    ignore_board_status(board_ped_read_event(event_buffer, sizeof(event_buffer), &bytes_read));
    ignore_board_status(board_ped_reset_trigger());
}

void action_update_observe_config(void) {
}

void action_accept_time_sync(void) {
}

void action_accept_orbit(void) {
}

void action_accept_attitude(void) {
}

void action_accept_magfield(void) {
}

void action_finish_observe_full(void) {
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_finish_observe(void) {
    ignore_board_status(board_ped_set_inhibit(1U));
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_finish_observe_alarm(void) {
    ignore_board_status(board_ped_set_inhibit(1U));
    ignore_board_status(board_nand_disconnect(default_nand_bank));
}

void action_finish_dump(void) {
    size_t bytes_written = 0U;

    ignore_board_status(board_usb_write(NULL, 0U, &bytes_written));
}

void action_finish_dump_alarm(void) {
    size_t bytes_written = 0U;

    ignore_board_status(board_usb_write(NULL, 0U, &bytes_written));
}

void action_fix_dump_results(void) {
}

void action_clear_alarm_status(void) {
}

void action_mark_alarm_exit(void) {
}

