#include "board_api.h"

#include "rtc.h"

BoardStatus board_rtc_get_time(InstrumentTime* time) {
    return rtc_get_time(time);
}

BoardStatus board_rtc_set_time(const InstrumentTime* time) {
    return rtc_set_time(time);
}
