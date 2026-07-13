#ifndef NATALIA_CORE_ACTION_H
#define NATALIA_CORE_ACTION_H

#include "../../BSP/Board_API/include/board_api.h"
#include "state.h"
#include "alarm.h"
#include "transport.h"

typedef enum {
    ACTION_OK = 0,
    ACTION_ERR_CONTENT,
    ACTION_ERR_OTHER,
    ACTION_ALARM
} ActionResult;

ActionResult action_init_hardware(void);

ActionResult action_load_mram(SystemContext* ctx);

ActionResult action_check_mram(SystemContext* ctx);

ActionResult action_restore_mram_copy(SystemContext* ctx);

ActionResult action_mark_init_done(SystemContext* ctx);

ActionResult action_mark_init_fail(SystemContext* ctx);

ActionResult action_mark_alarm(SystemContext* ctx);

ActionResult action_enter_safe_config(SystemContext* ctx);

ActionResult action_send_status(const SystemContext* ctx);

ActionResult action_send_ack(const SystemEvent* event);

ActionResult action_send_ack_status(const SystemEvent* event, TransportAckStatus status);

ActionResult action_send_telem(void);

ActionResult action_set_time(const SystemEvent* event);

ActionResult action_apply_config(SystemContext* ctx, const SystemEvent* event);

ActionResult action_write_mram(const SystemContext* ctx);

ActionResult action_recalc_masked_alarm(SystemContext* ctx);

ActionResult action_start_observe(SystemContext* ctx, const SystemEvent* event);

ActionResult action_start_erase(SystemContext* ctx, const SystemEvent* event);

ActionResult action_start_test(SystemContext* ctx, const SystemEvent* event);

ActionResult action_start_dump(SystemContext* ctx, const SystemEvent* event);

ActionResult action_start_shutdown(SystemContext* ctx);

ActionResult action_send_test_result(void);

ActionResult action_finish_erase(SystemContext* ctx, const SystemEvent* event);

ActionResult action_finish_erase_alarm(SystemContext* ctx);

ActionResult action_update_nand_state(SystemContext* ctx);

ActionResult action_clear_nand_full_flag(SystemContext* ctx);

ActionResult action_update_service_data(const SystemContext* ctx);

ActionResult action_finish_test(SystemContext* ctx, const SystemEvent* event);

ActionResult action_finish_test_alarm(SystemContext* ctx);

ActionResult action_update_test_results(SystemContext* ctx);

ActionResult action_observe_periodic(SystemContext* ctx);

ActionResult action_handle_ped_trigger(SystemContext* ctx);

ActionResult action_update_observe_config(SystemContext* ctx, const SystemEvent* event);

ActionResult action_accept_time_sync(SystemContext* ctx, const SystemEvent* event);

ActionResult action_accept_orbit(SystemContext* ctx, const SystemEvent* event);


ActionResult action_accept_magfield(SystemContext* ctx, const SystemEvent* event);

ActionResult action_finish_observe_full(SystemContext* ctx);

ActionResult action_finish_observe(SystemContext* ctx, const SystemEvent* event);

ActionResult action_finish_observe_alarm(SystemContext* ctx);

ActionResult action_finish_dump(SystemContext* ctx, const SystemEvent* event);

ActionResult action_finish_dump_alarm(SystemContext* ctx);

ActionResult action_fix_dump_results(SystemContext* ctx);

ActionResult action_clear_alarm_status(SystemContext* ctx);

ActionResult action_mark_alarm_exit(SystemContext* ctx);

#endif //NATALIA_CORE_ACTION_H
