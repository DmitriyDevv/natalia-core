#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "board_stub.h"
#include "event_queue.h"
#include "state.h"

/* §2: the flight loop must drain the RTC 1 Hz and PED trigger counters through
 * Board_API and enqueue one event per accumulated count. RTC ticks feed every
 * mode (heartbeat / future monitoring); PED triggers are collected only in
 * OBSERVE, their only consumer. */

static void rtc_collected_in_duty_ped_skipped(void) {
    SystemContext ctx;
    SystemEvent event;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_rtc_1hz_events(3U);
    board_stub_set_ped_trigger_events(5U); /* must be ignored outside OBSERVE */

    algorithm_collect_hw_events(&ctx);

    assert(system_event_queue_get_count() == 3U);
    while (system_event_queue_pop(&event)) {
        assert(event.type == EVENT_RTC_1HZ);
    }
}

static void rtc_and_ped_collected_in_observe(void) {
    SystemContext ctx;
    SystemEvent event;
    uint32_t rtc_seen = 0U;
    uint32_t ped_seen = 0U;

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_OBSERVE;
    board_stub_reset_all();
    system_event_queue_init();

    board_stub_set_rtc_1hz_events(1U);
    board_stub_set_ped_trigger_events(4U);

    algorithm_collect_hw_events(&ctx);

    assert(system_event_queue_get_count() == 5U);
    while (system_event_queue_pop(&event)) {
        if (event.type == EVENT_RTC_1HZ) {
            ++rtc_seen;
        } else if (event.type == EVENT_PED_TRIGGER) {
            ++ped_seen;
        } else {
            assert(0);
        }
    }

    assert(rtc_seen == 1U);
    assert(ped_seen == 4U);
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
    assert(system_event_queue_get_count() == 4U);

    /* Second collection with nothing new pending enqueues nothing. */
    system_event_queue_init();
    algorithm_collect_hw_events(&ctx);
    assert(system_event_queue_get_count() == 0U);
}

int main(void) {
    rtc_collected_in_duty_ped_skipped();
    rtc_and_ped_collected_in_observe();
    take_drains_counter();

    return 0;
}
