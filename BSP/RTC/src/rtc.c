#include "rtc.h"

#include <stdint.h>
#include <stddef.h>

#include "rtc_private.h"
#include "stm32l496xx.h"


static volatile uint32_t rtc_1hz_pending_count = 0U;

static volatile uint32_t rtc_boundary_seconds = 0U;

static uint64_t rtc_anchor_raw_ms = 0ULL;
static uint64_t rtc_anchor_instrument_ms = 0ULL;

void RTC_WKUP_IRQHandler(void);

static uint32_t rtc_ssr_to_milliseconds(uint32_t ssr) {
    uint32_t elapsed_ticks;

    elapsed_ticks = RTC_PREDIV_S_VALUE - (ssr & RTC_SSR_SS);

    return (elapsed_ticks * 1000UL) / RTC_SUBSECOND_TICKS_PER_SECOND;
}

static uint64_t rtc_read_raw_time_ms_locked(void) {
    uint32_t seconds;
    uint32_t ssr;
    uint32_t wutf_before;
    uint32_t wutf_after;
    uint32_t milliseconds;

    do {
        seconds = rtc_boundary_seconds;
        wutf_before = RTC->ISR & RTC_ISR_WUTF;
        ssr = RTC->SSR;
        wutf_after = RTC->ISR & RTC_ISR_WUTF;
    }
    while (wutf_before != wutf_after);

    if (wutf_after != 0U) {
        ++seconds;
    }

    milliseconds = rtc_ssr_to_milliseconds(ssr);

    return ((uint64_t)seconds * 1000ULL) + (uint64_t)milliseconds;
}

static uint64_t rtc_read_raw_time_ms(void) {
    uint32_t primask;
    uint64_t raw_time_ms;

    primask = __get_PRIMASK();
    __disable_irq();

    raw_time_ms = rtc_read_raw_time_ms_locked();

    if (primask == 0U) {
        __enable_irq();
    }

    return raw_time_ms;
}

BoardStatus rtc_init(void) {
    BoardStatus status;
    uint32_t primask;

    status = rtc_configure_hardware();
    if (status != BOARD_OK) {
        return status;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    rtc_1hz_pending_count = 0U;
    rtc_boundary_seconds = 0U;

    rtc_anchor_raw_ms = rtc_read_raw_time_ms_locked();
    rtc_anchor_instrument_ms = 0ULL;

    if (primask == 0U) {
        __enable_irq();
    }

    return BOARD_OK;
}

BoardStatus rtc_get_time(InstrumentTime* time) {
    uint64_t raw_time_ms;
    uint64_t elapsed_ms;
    uint64_t instrument_time_ms;
    uint64_t seconds;

    if (time == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    raw_time_ms = rtc_read_raw_time_ms();

    if (raw_time_ms < rtc_anchor_raw_ms) {
        return BOARD_ERR_IO;
    }

    elapsed_ms = raw_time_ms - rtc_anchor_raw_ms;
    instrument_time_ms = rtc_anchor_instrument_ms + elapsed_ms;

    seconds = instrument_time_ms / 1000ULL;

    if (seconds > (uint64_t)UINT32_MAX) {
        return BOARD_ERR_IO;
    }

    time->seconds = (uint32_t)seconds;
    time->milliseconds = (uint16_t)(instrument_time_ms % 1000ULL);

    return BOARD_OK;
}

BoardStatus rtc_set_time(const InstrumentTime* time) {
    uint32_t primask;
    uint64_t raw_time_ms;

    if (time == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (time->milliseconds >= 1000U) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    raw_time_ms = rtc_read_raw_time_ms_locked();

    rtc_anchor_raw_ms = raw_time_ms;
    rtc_anchor_instrument_ms =
        ((uint64_t)time->seconds * 1000ULL) +
        (uint64_t)time->milliseconds;

    if (primask == 0U) {
        __enable_irq();
    }

    return BOARD_OK;
}

uint32_t rtc_take_1hz_events(void) {
    uint32_t pending_count;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    pending_count = rtc_1hz_pending_count;
    rtc_1hz_pending_count = 0U;

    if (primask == 0U) {
        __enable_irq();
    }

    return pending_count;
}

void RTC_WKUP_IRQHandler(void) {
    if ((RTC->ISR & RTC_ISR_WUTF) == 0U) {
        return;
    }

    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;

    RTC->ISR &= ~RTC_ISR_WUTF;

    RTC->WPR = 0xFFU;

    EXTI->PR1 = EXTI_PR1_PIF20;

    if (rtc_boundary_seconds != UINT32_MAX) {
        ++rtc_boundary_seconds;
    }

    if (rtc_1hz_pending_count != UINT32_MAX) {
        ++rtc_1hz_pending_count;
    }
}
