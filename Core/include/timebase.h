#ifndef NATALIA_CORE_TIMEBASE_H
#define NATALIA_CORE_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

BoardStatus timebase_init(void);

uint32_t timebase_millis(void);

bool timebase_elapsed(uint32_t start_ms, uint32_t interval_ms);

void timebase_delay_ms_blocking(uint32_t delay_ms);

#endif /* NATALIA_CORE_TIMEBASE_H */