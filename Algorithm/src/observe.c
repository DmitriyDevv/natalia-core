#include "../include/observe.h"

#include "event_queue.h"

void observe_on_rtc_1hz(SystemContext *ctx) {
    (void)ctx;
    (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
}
