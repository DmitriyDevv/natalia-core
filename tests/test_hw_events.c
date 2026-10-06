#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "board_stub.h"
#include "event_queue.h"
#include "state.h"

/* §2: the flight loop drains the RTC 1 Hz counter through Board_API and enqueues
 * one event per accumulated count, in every mode. PED triggers never become
 * queue events: OBSERVE takes PED records from the ring in batches. */

static void rtc_collected_in_duty_ped_skipped(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_rtc_1hz_events(3U);
    board_stub_set_ped_trigger_events(5U);

    algorithm_collect_hw_events(&ctx);

    assert(system_event_queue_get_count() == 3U);
    while (system_event_queue_pop(&event)) {
        assert(event.type == EVENT_RTC_1HZ);
    }
}

static void ped_triggers_not_queued_in_observe(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_OBSERVE;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_rtc_1hz_events(1U);
    board_stub_set_ped_trigger_events(4U);

    algorithm_collect_hw_events(&ctx);

    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_RTC_1HZ);
}

static void take_drains_counter(void) {
    SystemContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_OBSERVE;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_rtc_1hz_events(2U);
    board_stub_set_ped_trigger_events(2U);

    algorithm_collect_hw_events(&ctx);
    assert(system_event_queue_get_count() == 2U);

    /* Second collection with nothing new pending enqueues nothing. */
    system_event_queue_init();
    algorithm_collect_hw_events(&ctx);
    assert(system_event_queue_get_count() == 0U);
}

static void nand_power_fault_raises_alarm_and_enters_alarm_mode(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    ctx.alarm_mask = ALARM_ALL_MASK;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_nand_power_fault(2U);

    algorithm_collect_hw_events(&ctx);

    assert((ctx.alarm_status & ALARM_NAND_PS) != 0U);
    assert((ctx.masked_alarm & ALARM_NAND_PS) != 0U);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_MASKED_ALARM_SET);

    /* The fault latch is consumed: a second collection raises nothing new. */
    algorithm_collect_hw_events(&ctx);
    assert(system_event_queue_get_count() == 0U);

    /* Through the state machine the masked alarm moves DUTY to ALARM. */
    (void)system_event_queue_push_back_type(EVENT_MASKED_ALARM_SET);
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_ALARM);
}

static void no_nand_power_fault_no_alarm(void) {
    SystemContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    ctx.alarm_mask = ALARM_ALL_MASK;
    board_stub_reset_all();
    system_event_queue_init();

    algorithm_collect_hw_events(&ctx);

    assert((ctx.alarm_status & ALARM_NAND_PS) == 0U);
    assert(system_event_queue_get_count() == 0U);
}

int main(void) {
    rtc_collected_in_duty_ped_skipped();
    ped_triggers_not_queued_in_observe();
    take_drains_counter();
    nand_power_fault_raises_alarm_and_enters_alarm_mode();
    no_nand_power_fault_no_alarm();

    return 0;
}
