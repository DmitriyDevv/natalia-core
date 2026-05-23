#include "../include/observe.h"

void observe_on_rtc_1hz(SystemContext *ctx) {
    const SystemEvent event = {
        .type = EVENT_RTC_1HZ,
        .msg_id = 0U
    };

    (void)handle_event(ctx, &event);
}
