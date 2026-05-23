#ifndef NATALIA_RTC_H
#define NATALIA_RTC_H

#include <stdint.h>

#include "instrument_time.h"
#include "status.h"


BoardStatus rtc_init(void);


BoardStatus rtc_get_time(InstrumentTime *time);


BoardStatus rtc_set_time(const InstrumentTime *time);


uint32_t rtc_take_1hz_events(void);

#endif /* NATALIA_RTC_H */