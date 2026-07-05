#include "event_queue.h"

#include <stddef.h>
#include <string.h>

#ifndef SYSTEM_EVENT_QUEUE_CAPACITY
#define SYSTEM_EVENT_QUEUE_CAPACITY (32U)
#endif

#if (SYSTEM_EVENT_QUEUE_CAPACITY < 2U)
#error "SYSTEM_EVENT_QUEUE_CAPACITY must be at least 2"
#endif

#if (SYSTEM_EVENT_QUEUE_CAPACITY > 65535U)
#error "SYSTEM_EVENT_QUEUE_CAPACITY must fit into uint16_t"
#endif

typedef struct {
    SystemEvent data[SYSTEM_EVENT_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    volatile uint32_t overflow_count;
} SystemEventQueue;

static SystemEventQueue system_event_queue;

static uint32_t event_queue_irq_save(void) {
#if defined(__arm__) || defined(__thumb__)
    uint32_t primask;

    __asm volatile ("MRS %0, PRIMASK" : "=r" (primask) :: "memory");
    __asm volatile ("cpsid i" ::: "memory");

    return primask;
#else
    return 0U;
#endif
}

static void event_queue_irq_restore(uint32_t primask) {
#if defined(__arm__) || defined(__thumb__)
    if ((primask & 1U) == 0U) {
        __asm volatile ("cpsie i" ::: "memory");
    }
#else
    (void)primask;
#endif
}

static uint16_t event_queue_next_index(uint16_t index) {
    uint16_t next;

    next = (uint16_t)(index + 1U);

    if (next >= (uint16_t)SYSTEM_EVENT_QUEUE_CAPACITY) {
        next = 0U;
    }

    return next;
}

static uint16_t event_queue_prev_index(uint16_t index) {
    if (index == 0U) {
        return (uint16_t)(SYSTEM_EVENT_QUEUE_CAPACITY - 1U);
    }

    return (uint16_t)(index - 1U);
}

void system_event_queue_init(void) {
    uint32_t primask;

    primask = event_queue_irq_save();

    (void)memset(&system_event_queue, 0, sizeof(system_event_queue));

    event_queue_irq_restore(primask);
}

bool system_event_queue_push_back(const SystemEvent* event) {
    uint32_t primask;
    bool result = false;

    if (event == NULL) {
        return false;
    }

    primask = event_queue_irq_save();

    if (system_event_queue.count < (uint16_t)SYSTEM_EVENT_QUEUE_CAPACITY) {
        system_event_queue.data[system_event_queue.tail] = *event;
        system_event_queue.tail = event_queue_next_index(system_event_queue.tail);
        ++system_event_queue.count;
        result = true;
    } else {
        ++system_event_queue.overflow_count;
    }

    event_queue_irq_restore(primask);

    return result;
}

bool system_event_queue_push_front(const SystemEvent* event) {
    uint32_t primask;
    bool result = false;

    if (event == NULL) {
        return false;
    }

    primask = event_queue_irq_save();

    if (system_event_queue.count < (uint16_t)SYSTEM_EVENT_QUEUE_CAPACITY) {
        system_event_queue.head = event_queue_prev_index(system_event_queue.head);
        system_event_queue.data[system_event_queue.head] = *event;
        ++system_event_queue.count;
        result = true;
    } else {
        ++system_event_queue.overflow_count;
    }

    event_queue_irq_restore(primask);

    return result;
}

bool system_event_queue_push_back_type(EventType type) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = 0U;

    return system_event_queue_push_back(&event);
}

bool system_event_queue_push_front_type(EventType type) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.type = type;
    event.msg_id = 0U;

    return system_event_queue_push_front(&event);
}

bool system_event_queue_pop(SystemEvent* event) {
    uint32_t primask;
    bool result = false;

    if (event == NULL) {
        return false;
    }

    primask = event_queue_irq_save();

    if (system_event_queue.count > 0U) {
        *event = system_event_queue.data[system_event_queue.head];
        system_event_queue.head = event_queue_next_index(system_event_queue.head);
        --system_event_queue.count;
        result = true;
    }

    event_queue_irq_restore(primask);

    return result;
}

uint32_t system_event_queue_get_count(void) {
    uint32_t primask;
    uint32_t count;

    primask = event_queue_irq_save();

    count = system_event_queue.count;

    event_queue_irq_restore(primask);

    return count;
}

uint32_t system_event_queue_get_overflow_count(void) {
    uint32_t primask;
    uint32_t overflow_count;

    primask = event_queue_irq_save();

    overflow_count = system_event_queue.overflow_count;

    event_queue_irq_restore(primask);

    return overflow_count;
}
