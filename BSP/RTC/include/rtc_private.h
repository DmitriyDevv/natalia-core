#ifndef NATALIA_RTC_PRIVATE_H
#define NATALIA_RTC_PRIVATE_H

#include <stdint.h>

#include "status.h"


#define RTC_SUBSECOND_TICKS_PER_SECOND (32768UL)
#define RTC_PREDIV_A_VALUE              (0UL)
#define RTC_PREDIV_S_VALUE              (32767UL)

BoardStatus rtc_configure_hardware(void);

#endif /* NATALIA_RTC_PRIVATE_H */