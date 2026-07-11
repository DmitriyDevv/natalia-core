#ifndef NATALIA_CORE_EVENT_QUEUE_H
#define NATALIA_CORE_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

void system_event_queue_init(void);
void system_event_queue_clear(void);

bool system_event_queue_push_back(const SystemEvent* event);
bool system_event_queue_push_front(const SystemEvent* event);

bool system_event_queue_push_back_type(EventType type);
bool system_event_queue_push_front_type(EventType type);

bool system_event_queue_pop(SystemEvent* event);

uint32_t system_event_queue_get_count(void);
uint32_t system_event_queue_get_overflow_count(void);

#endif