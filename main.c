#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "algorithm.h"
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

    handle_event(&ctx, &event);

    assert(ctx.state == STATE_ERASE);
    assert(ctx.previous_state == STATE_DUTY);
    assert(ctx.erase.bank == NAND_BANK_1);
    assert(ctx.erase.stage == ERASE_STAGE_WAIT);
}

static void duty_start_test_runs_to_done(void) {
    SystemContext ctx = {
        .state = STATE_DUTY,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };
    const SystemEvent event = {
        .type = EVENT_CMD_TEST,
        .msg_id = 108U,
        .command.test = {
            .bank = NAND_BANK_1,
            .power_after_done = POWER_AFTER_DONE_OFF,
            .test_mask = 0x5AU
        }
    };

    handle_event(&ctx, &event);

    assert(ctx.state == STATE_TEST);
    assert(ctx.test.stage == TEST_STAGE_WRITE);

    for (size_t i = 0U; (i < 128U) && (ctx.state == STATE_TEST); ++i) {
        algorithm_poll(&ctx);
    }

    assert(ctx.state == STATE_DUTY);
    assert(ctx.test.stage == TEST_STAGE_FINISH_OK);
    assert(ctx.test.result_valid);
    assert(ctx.test.result_status == TEST_RESULT_STATUS_OK);
    assert(ctx.test.total_errors == 0U);
    assert(ctx.test.block_index == TEST_MODE_BLOCK_COUNT);
}

static void duty_start_dump_runs_to_done(void) {
    SystemContext ctx = {
        .state = STATE_DUTY,
        .alarm_status = 0U,
        .masked_alarm = 0U
    };
    const SystemEvent event = {
        .type = EVENT_CMD_DUMP,
        .msg_id = 109U,
        .command.dump = {
            .bank = NAND_BANK_1,
            .power_after_done = POWER_AFTER_DONE_OFF,
            .start_address = 0U,
            .size = DUMP_MODE_PACKET_SIZE * 2U
        }
    };

    handle_event(&ctx, &event);

    assert(ctx.state == STATE_DUMP);
    assert(ctx.dump.stage == DUMP_STAGE_READ);

    for (size_t i = 0U; (i < 128U) && (ctx.state == STATE_DUMP); ++i) {
        algorithm_poll(&ctx);
    }

    assert(ctx.state == STATE_DUTY);
    assert(ctx.dump.stage == DUMP_STAGE_FINISH_OK);
    assert(ctx.dump.bytes_done == event.command.dump.size);
    assert(ctx.dump.last_dumped_packet == 2U);
    assert(ctx.usb.bytes_written == event.command.dump.size);
}

static volatile bool rtc_1hz_pending = false;

int main(void) {
    init_done_to_duty();
    init_done_to_alarm();
    init_fail_to_alarm();
    duty_start_erase_with_payload();
    duty_start_test_runs_to_done();
    duty_start_dump_runs_to_done();

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
