#ifndef NATALIA_CORE_INSTRUMENT_TIME_H
#define NATALIA_CORE_INSTRUMENT_TIME_H

#include <stdint.h>

typedef struct {
    uint32_t seconds;
    uint16_t milliseconds;
} InstrumentTime;

#endif /* NATALIA_CORE_INSTRUMENT_TIME_H */