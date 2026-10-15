#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "event_queue.h"
#include "state.h"

/* T-01: the fixed-size event queue every command and internal event goes
 * through, plus the deferred posting of one-shot events (completion events and
 * the masked-alarm edge) that must survive a full queue. */

#define CAPACITY 32U

static SystemEvent typed(EventType type, uint16_t msg_id) {
    SystemEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = msg_id;
    return event;
}

static void fill(uint32_t count) {
    uint32_t i;

    for (i = 0U; i < count; ++i) {
        SystemEvent event = typed(EVENT_RTC_1HZ, (uint16_t)i);
        assert(system_event_queue_push_back(&event));
    }
}

static void fifo_order(void) {
    SystemEvent a = typed(EVENT_CMD_DUTY, 1U);
    SystemEvent b = typed(EVENT_CMD_ERASE, 2U);
    SystemEvent c = typed(EVENT_CMD_TEST, 3U);
    SystemEvent out;

    system_event_queue_init();
    assert(system_event_queue_push_back(&a));
    assert(system_event_queue_push_back(&b));
    assert(system_event_queue_push_back(&c));

    assert(system_event_queue_pop(&out) && (out.msg_id == 1U));
    assert(system_event_queue_pop(&out) && (out.msg_id == 2U));
    assert(system_event_queue_pop(&out) && (out.msg_id == 3U));
}

static void push_front_goes_first(void) {
    SystemEvent a = typed(EVENT_CMD_DUTY, 1U);
    SystemEvent b = typed(EVENT_MASKED_ALARM_SET, 2U);
    SystemEvent out;

    system_event_queue_init();
    assert(system_event_queue_push_back(&a));
    assert(system_event_queue_push_front(&b));

    assert(system_event_queue_pop(&out) && (out.type == EVENT_MASKED_ALARM_SET));
    assert(system_event_queue_pop(&out) && (out.type == EVENT_CMD_DUTY));
}

static void fill_to_capacity_and_overflow(void) {
    SystemEvent extra = typed(EVENT_CMD_SHUTDOWN, 999U);
    SystemEvent out;
    uint32_t i;

    system_event_queue_init();
    fill(CAPACITY);
    assert(system_event_queue_get_count() == CAPACITY);

    assert(!system_event_queue_push_back(&extra));
    assert(!system_event_queue_push_front(&extra));
    assert(system_event_queue_get_overflow_count() == 2U);
    assert(system_event_queue_get_count() == CAPACITY);

    for (i = 0U; i < CAPACITY; ++i) {
        assert(system_event_queue_pop(&out));
        assert(out.type == EVENT_RTC_1HZ);
        assert(out.msg_id == (uint16_t)i);
    }
    assert(!system_event_queue_pop(&out));
}

static void pop_on_empty_leaves_output(void) {
    SystemEvent out = typed(EVENT_CMD_TEST, 77U);

    system_event_queue_init();
    assert(!system_event_queue_pop(&out));
    assert(out.type == EVENT_CMD_TEST);
    assert(out.msg_id == 77U);
}

static void wraparound_keeps_order(void) {
    SystemEvent out;
    uint32_t i;

    system_event_queue_init();
    fill(5U);
    for (i = 0U; i < (3U * CAPACITY); ++i) {
        SystemEvent next = typed(EVENT_RTC_1HZ, (uint16_t)(i + 5U));

        assert(system_event_queue_push_back(&next));
        assert(system_event_queue_pop(&out) && (out.msg_id == (uint16_t)i));
        assert(system_event_queue_get_count() == 5U);
    }
}

static void clear_keeps_overflow_init_resets_it(void) {
    SystemEvent extra = typed(EVENT_CMD_SHUTDOWN, 0U);

    system_event_queue_init();
    fill(CAPACITY);
    assert(!system_event_queue_push_back(&extra));

    system_event_queue_clear();
    assert(system_event_queue_get_count() == 0U);
    assert(system_event_queue_get_overflow_count() == 1U);

    system_event_queue_init();
    assert(system_event_queue_get_overflow_count() == 0U);
}

static void typed_pushes_zero_payload(void) {
    SystemEvent out;

    system_event_queue_init();
    assert(system_event_queue_push_back_type(EVENT_ERASE_DONE));
    assert(system_event_queue_push_front_type(EVENT_TEST_DONE));

    assert(system_event_queue_pop(&out));
    assert(out.type == EVENT_TEST_DONE);
    assert(out.msg_id == 0U);
    assert(out.command.erase.bank == NAND_BANK_NONE);

    assert(system_event_queue_pop(&out));
    assert(out.type == EVENT_ERASE_DONE);
}

static void command_payload_round_trip(void) {
    SystemEvent in;
    SystemEvent out;

    memset(&in, 0, sizeof(in));
    in.type = EVENT_CMD_DUMP;
    in.msg_id = 0x0F06U;
    in.command.dump.bank = NAND_BANK_2;
    in.command.dump.size = 4096U;
    in.command.dump.dump_all = true;

    system_event_queue_init();
    assert(system_event_queue_push_back(&in));
    assert(system_event_queue_pop(&out));
    assert(memcmp(&in, &out, sizeof(in)) == 0);
}

/* A one-shot event posted into a full queue is not lost: it is kept as pending
 * and enters the queue as soon as a slot is freed, after the events that were
 * already waiting. */
static void posted_event_survives_full_queue(void) {
    SystemEvent out;
    uint32_t i;

    system_event_queue_init();
    fill(CAPACITY);

    assert(!system_event_queue_post_type(EVENT_ERASE_DONE));
    assert(!system_event_queue_post_type(EVENT_MASKED_ALARM_SET));
    assert(system_event_queue_get_pending_types() ==
           ((1UL << (uint32_t)EVENT_ERASE_DONE) | (1UL << (uint32_t)EVENT_MASKED_ALARM_SET)));

    for (i = 0U; i < CAPACITY; ++i) {
        assert(system_event_queue_pop(&out));
        assert(out.type == EVENT_RTC_1HZ);
    }

    assert(system_event_queue_pop(&out) && (out.type == EVENT_ERASE_DONE));
    assert(system_event_queue_pop(&out) && (out.type == EVENT_MASKED_ALARM_SET));
    assert(!system_event_queue_pop(&out));
    assert(system_event_queue_get_pending_types() == 0U);
}

static void post_with_room_is_plain_push(void) {
    SystemEvent out;

    system_event_queue_init();
    assert(system_event_queue_post_type(EVENT_NAND_FULL));
    assert(system_event_queue_get_pending_types() == 0U);
    assert(system_event_queue_pop(&out) && (out.type == EVENT_NAND_FULL));
}

static void clear_drops_pending(void) {
    SystemEvent out;

    system_event_queue_init();
    fill(CAPACITY);
    assert(!system_event_queue_post_type(EVENT_DUMP_DONE));

    system_event_queue_clear();
    assert(system_event_queue_get_pending_types() == 0U);
    assert(!system_event_queue_pop(&out));
}

int main(void) {
    fifo_order();
    push_front_goes_first();
    fill_to_capacity_and_overflow();
    pop_on_empty_leaves_output();
    wraparound_keeps_order();
    clear_keeps_overflow_init_resets_it();
    typed_pushes_zero_payload();
    command_payload_round_trip();
    posted_event_survives_full_queue();
    post_with_room_is_plain_push();
    clear_drops_pending();

    return 0;
}
