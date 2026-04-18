#ifndef NATALIA_CORE_ACTION_H
#define NATALIA_CORE_ACTION_H

void action_init_hardware(void);

void action_load_mram(void);

void action_check_mram(void);

void action_restore_mram_copy(void);

void action_mark_init_done(void);

void action_mark_init_fail(void);

void action_mark_alarm(void);

void action_enter_safe_config(void);

void action_send_status(void);

void action_send_ack(void);

void action_send_ack_error(void);

void action_send_telem(void);

void action_set_time(void);

void action_apply_config(void);

void action_write_mram(void);

void action_recalc_masked_alarm(void);

void action_start_observe(void);

void action_start_erase(void);

void action_start_test(void);

void action_start_dump(void);

void action_start_shutdown(void);

void action_send_test_result(void);

#endif //NATALIA_CORE_ACTION_H
