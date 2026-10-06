#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "alarm.h"
#include "algorithm.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "state.h"
#include "transport.h"

#define SETTLE_GUARD 5000U

static void begin_test(SystemContext *ctx, uint32_t alarm_mask) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_DUTY;
    ctx->alarm_mask = alarm_sanitize_mask(alarm_mask);
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;

    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static void enqueue_erase(void) {
    SystemEvent event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_ERASE;
    event.command.erase.bank = NAND_BANK_1;
    event.command.erase.power_after_done = POWER_AFTER_DONE_OFF;
    assert(system_event_queue_push_back(&event));
}

static void enqueue_test(void) {
    SystemEvent event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_TEST;
    event.command.test.bank = NAND_BANK_1;
    event.command.test.power_after_done = POWER_AFTER_DONE_OFF;
    assert(system_event_queue_push_back(&event));
}

static void enqueue_dump(uint32_t packet_count) {
    SystemEvent event;
    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_DUMP;
    event.command.dump.bank = NAND_BANK_1;
    event.command.dump.power_after_done = POWER_AFTER_DONE_OFF;
    event.command.dump.requested_packet_count = packet_count;
    event.command.dump.size = packet_count * DUMP_MODE_PACKET_SIZE;
    assert(system_event_queue_push_back(&event));
}

static void step(SystemContext *ctx) {
    algorithm_poll(ctx);
    algorithm_process_events(ctx);
}

/* Runs the mode started from DUTY until the FSM leaves it. */
static void run_until_settled(SystemContext *ctx, SystemState mode_state) {
    uint32_t guard;
    bool entered = (ctx->state == mode_state);

    for (guard = 0U; guard < SETTLE_GUARD; ++guard) {
        step(ctx);
        if (ctx->state == mode_state) {
            entered = true;
        } else if (entered) {
            return;
        }
    }

    assert(false);
}

static bool nand1_powered(void) {
    uint8_t is_powered = 1U;

    assert(board_nand_is_powered(1U, &is_powered) == BOARD_OK);

    return is_powered != 0U;
}

/* A NAND error reported while erasing raises ALARM_NAND_PR and ends in ALARM
 * with the bank switched off. */
static void erase_failure_raises_nand_pr(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_nand_erase_status(1U, BOARD_ERR_IO);
    enqueue_erase();
    run_until_settled(&ctx, STATE_ERASE);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_NAND_PR) != 0U);
    assert(ctx.masked_alarm != 0U);
    assert(!nand1_powered());
}

/* A NAND write error in TEST mode raises ALARM_NAND_PR. */
static void test_write_failure_raises_nand_pr(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_nand_write_fail_at(1U, 3U);
    enqueue_test();
    run_until_settled(&ctx, STATE_TEST);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_NAND_PR) != 0U);
    assert(!nand1_powered());
}

/* A NAND read error in DUMP raises ALARM_NAND_PR. */
static void dump_read_failure_raises_nand_pr(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_nand_read_status(1U, BOARD_ERR_IO);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_NAND_PR) != 0U);
    assert((ctx.alarm_status & ALARM_USB_PR) == 0U);
    assert(!board_stub_data_link_is_open());
}

/* An uncorrectable-ECC page is sent as it is: the packet carries its own CRC
 * and the ground checks it. DUMP completes normally. */
static void dump_uncorrectable_ecc_does_not_stop_dump(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_nand_read_status(1U, BOARD_ERR_CRC);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.alarm_status == 0U);
    assert(ctx.dump.bytes_done == (4U * DUMP_MODE_PACKET_SIZE));
    assert(!nand1_powered());
}

/* A completed DUMP switches the output interface off. */
static void dump_completion_closes_link(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.alarm_status == 0U);
    assert(!board_stub_data_link_is_open());
}

/* A data-output error in DUMP raises ALARM_USB_PR and shuts the output
 * interface down (lines off, reset, power off). */
static void dump_output_failure_raises_usb_pr(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_write_status(BOARD_ERR_IO);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_USB_PR) != 0U);
    assert((ctx.alarm_status & ALARM_NAND_PR) == 0U);
    assert(!board_stub_data_link_is_open());
}

/* DUMP sends nothing until the output link reports ready (time for the host to
 * open the port); the wait is not counted by the transfer timeout. */
static void dump_waits_for_link_ready(void) {
    SystemContext ctx;
    uint32_t i;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_link_ready(false);
    enqueue_dump(4U);

    for (i = 0U; i < 20U; ++i) {
        step(&ctx);
    }
    for (i = 0U; i < (DUMP_MODE_TX_TIMEOUT_S + 2U); ++i) {
        (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
        step(&ctx);
    }

    assert(ctx.state == STATE_DUMP);
    assert(ctx.dump.bytes_done == 0U);
    assert(ctx.alarm_status == 0U);

    board_stub_set_data_link_ready(true);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_DUTY);
    assert(ctx.alarm_status == 0U);
    assert(ctx.dump.bytes_done == (4U * DUMP_MODE_PACKET_SIZE));
}

/* DUMP start opens the output interface. */
static void dump_start_opens_link(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    enqueue_dump(4U);
    step(&ctx);

    assert(ctx.state == STATE_DUMP);
    assert(board_stub_data_link_is_open());
}

/* No receiver (PU_USB_VBUS = HIGH) before DUMP: ALARM_USB_VBUS, DUMP is not
 * entered, the FSM goes to ALARM. */
static void dump_without_receiver_enters_alarm(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_link_present(false);
    enqueue_dump(4U);
    step(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_USB_VBUS) != 0U);
    assert(!nand1_powered());
}

/* The same with ALARM_USB_VBUS masked: DUMP runs, the bit stays in the alarm
 * status. */
static void dump_without_receiver_masked_runs(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK & ~ALARM_USB_VBUS);
    board_stub_set_data_link_present(false);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_DUTY);
    assert((ctx.alarm_status & ALARM_USB_VBUS) != 0U);
    assert(ctx.masked_alarm == 0U);
    assert(ctx.dump.bytes_done == (4U * DUMP_MODE_PACKET_SIZE));
}

/* Receiver unplugged during DUMP (VBUS rising edge): ALARM_USB_VBUS, ALARM,
 * output interface shut down, bank off. */
static void receiver_unplugged_during_dump_enters_alarm(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_write_stalled(true);
    enqueue_dump(4U);
    step(&ctx);
    step(&ctx);
    assert(ctx.state == STATE_DUMP);

    board_stub_set_data_link_fault();
    algorithm_collect_hw_events(&ctx);
    step(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_USB_VBUS) != 0U);
    assert(!board_stub_data_link_is_open());
    assert(!nand1_powered());
}

/* A failing end-of-dump flush raises ALARM_USB_PR, powers the bank off and
 * leaves DUMP. */
static void dump_flush_failure_leaves_dump(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_flush_status(BOARD_ERR_UNSUPPORTED);
    enqueue_dump(4U);
    run_until_settled(&ctx, STATE_DUMP);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_USB_PR) != 0U);
    assert(ctx.dump.bytes_done == (4U * DUMP_MODE_PACKET_SIZE));
    assert(!nand1_powered());
    assert(!board_stub_data_link_is_open());
}

/* Output that accepts nothing (receiver not reading) times out after
 * DUMP_MODE_TX_TIMEOUT_S RTC seconds without progress and raises ALARM_USB_PR. */
static void dump_stalled_output_times_out(void) {
    SystemContext ctx;
    uint32_t second;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_write_stalled(true);
    enqueue_dump(4U);

    step(&ctx);
    step(&ctx);
    assert(ctx.state == STATE_DUMP);

    for (second = 1U; second < DUMP_MODE_TX_TIMEOUT_S; ++second) {
        board_stub_set_rtc_1hz_events(1U);
        algorithm_collect_hw_events(&ctx);
        step(&ctx);
        assert(ctx.state == STATE_DUMP);
    }

    board_stub_set_rtc_1hz_events(1U);
    algorithm_collect_hw_events(&ctx);
    step(&ctx);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_USB_PR) != 0U);
    assert(!nand1_powered());
}

/* RTC ticks that piled up while the super-loop was blocked arrive as a burst
 * with no send attempt between them: they are not counted as stalled seconds. */
static void dump_rtc_tick_burst_is_not_a_timeout(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    board_stub_set_data_write_stalled(true);
    enqueue_dump(4U);
    step(&ctx);
    assert(ctx.state == STATE_DUMP);

    board_stub_set_rtc_1hz_events(3U * DUMP_MODE_TX_TIMEOUT_S);
    algorithm_collect_hw_events(&ctx);
    step(&ctx);
    step(&ctx);
    step(&ctx);

    assert(ctx.state == STATE_DUMP);
    assert(ctx.alarm_status == 0U);
}

/* Output that keeps moving is never timed out, however many seconds pass. */
static void dump_progressing_output_is_not_timed_out(void) {
    SystemContext ctx;
    uint32_t second;

    begin_test(&ctx, ALARM_ALL_MASK);
    enqueue_dump(64U);
    step(&ctx);

    for (second = 0U; (second < (2U * DUMP_MODE_TX_TIMEOUT_S)) && (ctx.state == STATE_DUMP); ++second) {
        board_stub_set_rtc_1hz_events(1U);
        algorithm_collect_hw_events(&ctx);
        step(&ctx);
    }

    run_until_settled(&ctx, STATE_DUMP);
    assert(ctx.state == STATE_DUTY);
    assert(ctx.alarm_status == 0U);
}

/* An MRAM save failure on ERASE completion raises ALARM_MRAM; unmasked, the FSM
 * enters ALARM. */
static void mram_failure_on_completion_enters_alarm(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK);
    enqueue_erase();
    step(&ctx);
    assert(ctx.state == STATE_ERASE);

    board_stub_set_mram_write_fail(true);
    run_until_settled(&ctx, STATE_ERASE);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert(!nand1_powered());
}

/* The same failure with ALARM_MRAM masked: the mode still ends (DUTY), the bit
 * stays visible in the alarm status. */
static void masked_mram_failure_on_completion_ends_in_duty(void) {
    SystemContext ctx;

    begin_test(&ctx, ALARM_ALL_MASK & ~ALARM_MRAM);
    enqueue_erase();
    step(&ctx);
    assert(ctx.state == STATE_ERASE);

    board_stub_set_mram_write_fail(true);
    run_until_settled(&ctx, STATE_ERASE);

    assert(ctx.state == STATE_DUTY);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert(ctx.masked_alarm == 0U);
    assert(!nand1_powered());
}

int main(void) {
    erase_failure_raises_nand_pr();
    test_write_failure_raises_nand_pr();
    dump_read_failure_raises_nand_pr();
    dump_uncorrectable_ecc_does_not_stop_dump();
    dump_output_failure_raises_usb_pr();
    dump_start_opens_link();
    dump_waits_for_link_ready();
    dump_completion_closes_link();
    dump_without_receiver_enters_alarm();
    dump_without_receiver_masked_runs();
    receiver_unplugged_during_dump_enters_alarm();
    dump_flush_failure_leaves_dump();
    dump_stalled_output_times_out();
    dump_rtc_tick_burst_is_not_a_timeout();
    dump_progressing_output_is_not_timed_out();
    mram_failure_on_completion_enters_alarm();
    masked_mram_failure_on_completion_ends_in_duty();
    return 0;
}
