#ifndef NATALIA_CORE_ALARM_MONITOR_H
#define NATALIA_CORE_ALARM_MONITOR_H

#include <stdint.h>

#include "state.h"

/* Sets an alarm bit and recomputes MaskedAlarm. Does NOT enqueue an event; use
 * from command-time detection points that already return ACTION_ALARM. */
void alarm_set(SystemContext *ctx, uint32_t bit);

/* alarm_set plus, on a 0 -> nonzero MaskedAlarm edge, enqueues
 * EVENT_MASKED_ALARM_SET. Use from background polling (the 20 s monitor). */
void alarm_raise(SystemContext *ctx, uint32_t bit);

/* 20 s parameter-monitoring cycle, gated on now_ms. In DUTY/ERASE/TEST/OBSERVE/
 * DUMP/ALARM it reads temperatures, voltages, currents and PSON states through
 * Board_API and raises the matching alarm bits when out of range. */
void alarm_monitor_poll(SystemContext *ctx, uint32_t now_ms);

#endif /* NATALIA_CORE_ALARM_MONITOR_H */
