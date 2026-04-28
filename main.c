#include <assert.h>
#include <stdbool.h>

#include "observe.h"
#include "state.h"


static void init_done_to_duty(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };

    handle_event(&ctx, EVENT_INIT_DONE);

    assert(ctx.state == STATE_DUTY);
}

static void init_done_to_alarm(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 1U
    };

    handle_event(&ctx, EVENT_INIT_DONE);

    assert(ctx.state == STATE_ALARM);
}

static void init_fail_to_alarm(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };

    handle_event(&ctx, EVENT_INIT_FAIL);

    assert(ctx.state == STATE_ALARM);
}

static volatile bool rtc_1hz_pending = false;

int main(void) {
    init_done_to_duty();
    init_done_to_alarm();
    init_fail_to_alarm();

    SystemContext ctx = {
        .state = STATE_OBSERVE,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };

    rtc_1hz_pending = true;

    if (rtc_1hz_pending) {
        rtc_1hz_pending = false;

        if (ctx.state == STATE_OBSERVE) {
            observe_on_rtc_1hz(&ctx);
        }
    }


    return 0;
}

