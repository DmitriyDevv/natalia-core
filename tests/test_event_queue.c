#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "event_queue.h"

#define EVENT_QUEUE_CAPACITY 32U

static SystemEvent make_event(EventType type, uint32_t msg_id) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = msg_id;

    return event;
}

static void assert_empty_command_payload(const SystemEvent* event) {
    uint32_t index;
    const uint8_t* bytes = (const uint8_t*)&event->command;

    for (index = 0U; index < sizeof(event->command); ++index) {
        assert(bytes[index] == 0U);
    }
}

static void fifo_order_is_preserved(void) {
    const SystemEvent expected[] = {
        { .type = EVENT_CMD_STATUS_REQ, .msg_id = 11U },
        { .type = EVENT_CMD_TELEM_REQ, .msg_id = 12U },
        { .type = EVENT_CMD_VERSION_REQ, .msg_id = 13U }
    };
    SystemEvent actual;
    uint32_t index;

    system_event_queue_init();

    for (index = 0U; index < 3U; ++index) {
        assert(system_event_queue_push_back(&expected[index]));
    }

    assert(system_event_queue_get_count() == 3U);
    for (index = 0U; index < 3U; ++index) {
        assert(system_event_queue_pop(&actual));
        assert(actual.type == expected[index].type);
        assert(actual.msg_id == expected[index].msg_id);
    }
    assert(system_event_queue_get_count() == 0U);
}

static void push_front_precedes_queued_events(void) {
    const SystemEvent first = make_event(EVENT_CMD_STATUS_REQ, 21U);
    const SystemEvent second = make_event(EVENT_CMD_TELEM_REQ, 22U);
    const SystemEvent urgent = make_event(EVENT_CMD_RESET_ALARM, 23U);
    SystemEvent actual;

    system_event_queue_init();

    assert(system_event_queue_push_back(&first));
    assert(system_event_queue_push_back(&second));
    assert(system_event_queue_push_front(&urgent));

    assert(system_event_queue_pop(&actual));
    assert(actual.type == urgent.type);
    assert(actual.msg_id == urgent.msg_id);
    assert(system_event_queue_pop(&actual));
    assert(actual.msg_id == first.msg_id);
    assert(system_event_queue_pop(&actual));
    assert(actual.msg_id == second.msg_id);
}

static void capacity_and_overflow_preserve_queued_events(void) {
    SystemEvent actual;
    uint32_t index;

    system_event_queue_init();

    for (index = 0U; index < EVENT_QUEUE_CAPACITY; ++index) {
        const SystemEvent event = make_event(EVENT_CMD_DUTY, index);

        assert(system_event_queue_push_back(&event));
    }

    assert(system_event_queue_get_count() == EVENT_QUEUE_CAPACITY);
    assert(system_event_queue_get_overflow_count() == 0U);

    assert(!system_event_queue_push_back(
        &(SystemEvent){ .type = EVENT_CMD_SHUTDOWN, .msg_id = EVENT_QUEUE_CAPACITY }));
    assert(system_event_queue_get_count() == EVENT_QUEUE_CAPACITY);
    assert(system_event_queue_get_overflow_count() == 1U);

    for (index = 0U; index < EVENT_QUEUE_CAPACITY; ++index) {
        assert(system_event_queue_pop(&actual));
        assert(actual.type == EVENT_CMD_DUTY);
        assert(actual.msg_id == index);
    }
    assert(!system_event_queue_pop(&actual));
}

static void empty_pop_leaves_output_unchanged(void) {
    SystemEvent actual;
    SystemEvent expected;

    system_event_queue_init();
    (void)memset(&actual, 0xA5, sizeof(actual));
    expected = actual;

    assert(!system_event_queue_pop(&actual));
    assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
}

static void count_and_ring_wrap_are_correct(void) {
    SystemEvent actual;
    uint32_t index;

    system_event_queue_init();

    for (index = 0U; index < (EVENT_QUEUE_CAPACITY * 3U); ++index) {
        const SystemEvent event = make_event(EVENT_RTC_1HZ, index);

        assert(system_event_queue_push_back(&event));
        assert(system_event_queue_get_count() == 1U);
        assert(system_event_queue_pop(&actual));
        assert(actual.msg_id == index);
        assert(system_event_queue_get_count() == 0U);
    }

    for (index = 0U; index < 7U; ++index) {
        const SystemEvent event = make_event(EVENT_PED_TRIGGER, index);

        assert(system_event_queue_push_back(&event));
    }
    assert(system_event_queue_get_count() == 7U);
    for (index = 0U; index < 3U; ++index) {
        assert(system_event_queue_pop(&actual));
        assert(system_event_queue_get_count() == (6U - index));
    }
}

static void clear_preserves_overflow_but_init_resets_it(void) {
    uint32_t index;

    system_event_queue_init();
    for (index = 0U; index < EVENT_QUEUE_CAPACITY; ++index) {
        const SystemEvent event = make_event(EVENT_CMD_DUMP, index);

        assert(system_event_queue_push_back(&event));
    }
    assert(!system_event_queue_push_back_type(EVENT_CMD_TEST));
    assert(system_event_queue_get_overflow_count() == 1U);

    system_event_queue_clear();
    assert(system_event_queue_get_count() == 0U);
    assert(system_event_queue_get_overflow_count() == 1U);

    system_event_queue_init();
    assert(system_event_queue_get_count() == 0U);
    assert(system_event_queue_get_overflow_count() == 0U);
}

static void type_helpers_zero_the_rest_of_the_event(void) {
    SystemEvent actual;

    system_event_queue_init();

    assert(system_event_queue_push_back_type(EVENT_ERASE_DONE));
    assert(system_event_queue_push_front_type(EVENT_TEST_DONE));

    assert(system_event_queue_pop(&actual));
    assert(actual.type == EVENT_TEST_DONE);
    assert(actual.msg_id == 0U);
    assert(actual.tlm_slot == 0U);
    assert_empty_command_payload(&actual);

    assert(system_event_queue_pop(&actual));
    assert(actual.type == EVENT_ERASE_DONE);
    assert(actual.msg_id == 0U);
    assert(actual.tlm_slot == 0U);
    assert_empty_command_payload(&actual);
}

static void command_payload_survives_round_trip(void) {
    SystemEvent expected = {
        .type = EVENT_CMD_DUMP,
        .msg_id = 0x12345678U,
        .tlm_slot = 4U,
        .command.dump = {
            .bank = NAND_BANK_2,
            .power_after_done = POWER_AFTER_DONE_KEEP,
            .start_address = 0x10203040U,
            .size = 0x50607080U,
            .requested_packet_count = 37U,
            .dump_all = true
        }
    };
    SystemEvent actual;

    system_event_queue_init();

    assert(system_event_queue_push_back(&expected));
    (void)memset(&expected, 0, sizeof(expected));

    assert(system_event_queue_pop(&actual));
    assert(actual.type == EVENT_CMD_DUMP);
    assert(actual.msg_id == 0x12345678U);
    assert(actual.tlm_slot == 4U);
    assert(actual.command.dump.bank == NAND_BANK_2);
    assert(actual.command.dump.power_after_done == POWER_AFTER_DONE_KEEP);
    assert(actual.command.dump.start_address == 0x10203040U);
    assert(actual.command.dump.size == 0x50607080U);
    assert(actual.command.dump.requested_packet_count == 37U);
    assert(actual.command.dump.dump_all);
}

int main(void) {
    fifo_order_is_preserved();
    push_front_precedes_queued_events();
    capacity_and_overflow_preserve_queued_events();
    empty_pop_leaves_output_unchanged();
    count_and_ring_wrap_are_correct();
    clear_preserves_overflow_but_init_resets_it();
    type_helpers_zero_the_rest_of_the_event();
    command_payload_survives_round_trip();

    return 0;
}
