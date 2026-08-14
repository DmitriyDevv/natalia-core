#include <assert.h>
#include <stdbool.h>

#include "board_stub.h"
#include "observe.h"
#include "state.h"


static void init_done_to_duty(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };
    const SystemEvent event = {
        .type = EVENT_INIT_DONE,
        .msg_id = 0U
    };

    board_stub_reset_all();
    handle_event(&ctx, &event);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.previous_state == STATE_INIT);
}

static void init_done_to_alarm(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 1U
    };
    const SystemEvent event = {
        .type = EVENT_INIT_DONE,
        .msg_id = 0U
    };

    board_stub_reset_all();
    handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
}

static void init_fail_to_alarm(void) {
    SystemContext ctx = {
        .state = STATE_INIT,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };
    const SystemEvent event = {
        .type = EVENT_INIT_FAIL,
        .msg_id = 0U
    };

    board_stub_reset_all();
    handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
}

static void duty_start_erase_with_payload(void) {
    SystemContext ctx = {
        .state = STATE_DUTY,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };
    const SystemEvent event = {
        .type = EVENT_CMD_ERASE,
        .msg_id = 107U,
        .command.erase = {
            .bank = NAND_BANK_1,
            .power_after_done = POWER_AFTER_DONE_OFF
        }
    };

    board_stub_reset_all();
    handle_event(&ctx, &event);

    assert(ctx.state == STATE_ERASE);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.erase.bank == NAND_BANK_1);
    assert(ctx.erase.stage == ERASE_STAGE_WAIT);
}

static volatile bool rtc_1hz_pending = false;

int main(void) {
    init_done_to_duty();
    init_done_to_alarm();
    init_fail_to_alarm();
    duty_start_erase_with_payload();

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
