#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "stm32l4xx.h"
#include "timebase.h"

#ifndef PED_STREAM_WITH_PED
#define PED_STREAM_WITH_PED 0
#endif

#ifndef PED_STREAM_CHECK_SECONDS
#define PED_STREAM_CHECK_SECONDS 10U
#endif

#ifndef PED_STREAM_CONF_TRIG
#define PED_STREAM_CONF_TRIG 0x0000U
#endif

#ifndef PED_STREAM_THRESHOLD
#define PED_STREAM_THRESHOLD 0x0000U
#endif

#define PED_STREAM_TAKE_CAPACITY   64U
#define PED_STREAM_PERIOD_MIN_MS   950U
#define PED_STREAM_PERIOD_MAX_MS   1050U
#define PED_STREAM_TIMEOUT_MS      3000U
#define PED_STREAM_ADDR_CONF_TRIG  0x80U
#define PED_STREAM_ADDR_THRESHOLD  0x81U

static BoardPedRecord ped_stream_records[PED_STREAM_TAKE_CAPACITY];

static void ped_stream_log_u32(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void ped_stream_halt(void) {
    while (1) {
        __asm volatile("nop");
    }
}

static void ped_stream_fail(const char *stage, uint32_t value) {
    debug_log_write("PED STREAM FAIL ");
    debug_log_write(stage);
    debug_log_write(" value=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
    ped_stream_halt();
}

static void ped_stream_log_hex(const char *label, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[11];
    uint32_t i;

    text[0] = '0';
    text[1] = 'x';
    for (i = 0U; i < 8U; ++i) {
        text[2U + i] = digits[(value >> (28U - (i * 4U))) & 0xFUL];
    }
    text[10] = '\0';

    debug_log_write(label);
    debug_log_write("=");
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void ped_stream_dump_exti(const char *stage) {
    debug_log_write("EXTI dump ");
    debug_log_write(stage);
    debug_log_write("\r\n");
    ped_stream_log_hex("SYSCFG_EXTICR1", SYSCFG->EXTICR[0]);
    ped_stream_log_hex("EXTI_IMR1", EXTI->IMR1);
    ped_stream_log_hex("EXTI_RTSR1", EXTI->RTSR1);
    ped_stream_log_hex("RCC_APB2ENR", RCC->APB2ENR);
    ped_stream_log_u32("nvic_exti2_enabled", NVIC_GetEnableIRQ(EXTI2_IRQn));
}

static uint32_t ped_stream_count_marks(uint32_t duration_ms) {
    BoardPedRecord record;
    size_t count;
    uint32_t marks = 0U;
    uint32_t start_ms = timebase_millis();

    while (!timebase_elapsed(start_ms, duration_ms)) {
        if ((board_ped_take_records(&record, 1U, &count) == BOARD_OK) && (count == 1U) &&
            (record.kind == BOARD_PED_RECORD_SECOND)) {
            ++marks;
        }
    }

    return marks;
}

static void ped_stream_clear_wutf(void) {
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
    RTC->ISR &= ~RTC_ISR_WUTF;
    RTC->WPR = 0xFFU;
    EXTI->PR1 = EXTI_PR1_PIF20;
}

static void ped_stream_diagnose(void) {
    BoardPedStats before;
    BoardPedStats after;
    uint32_t rtc_events = 0U;
    uint32_t start_ms;
    uint32_t wutf_seen = 0U;
    uint32_t pb2_high = 0U;
    uint32_t exti2_pending = 0U;

    debug_log_write("DIAG registers\r\n");
    ped_stream_log_hex("RCC_BDCR", RCC->BDCR);
    ped_stream_log_hex("RTC_CR", RTC->CR);
    ped_stream_log_hex("RTC_OR", RTC->OR);
    ped_stream_log_hex("RTC_ISR", RTC->ISR);
    ped_stream_log_hex("RTC_WUTR", RTC->WUTR);
    ped_stream_log_hex("RTC_PRER", RTC->PRER);
    ped_stream_log_hex("GPIOB_MODER", GPIOB->MODER);
    ped_stream_log_hex("GPIOB_AFRL", GPIOB->AFR[0]);
    ped_stream_log_hex("GPIOB_OTYPER", GPIOB->OTYPER);
    ped_stream_log_hex("GPIOB_IDR", GPIOB->IDR);
    ped_stream_log_hex("SYSCFG_EXTICR1", SYSCFG->EXTICR[0]);
    ped_stream_log_hex("EXTI_IMR1", EXTI->IMR1);
    ped_stream_log_hex("EXTI_RTSR1", EXTI->RTSR1);
    ped_stream_log_hex("EXTI_PR1", EXTI->PR1);
    ped_stream_log_u32("nvic_exti2_enabled", NVIC_GetEnableIRQ(EXTI2_IRQn));
    ped_stream_log_u32("nvic_rtc_wkup_enabled", NVIC_GetEnableIRQ(RTC_WKUP_IRQn));

    (void)board_rtc_take_1hz_events(&rtc_events);
    timebase_delay_ms_blocking(2500U);
    (void)board_rtc_take_1hz_events(&rtc_events);
    ped_stream_log_u32("rtc_1hz_irq_in_2500ms", rtc_events);

    debug_log_write("DIAG hold WUTF with RTC_WKUP irq disabled\r\n");
    (void)board_ped_get_stats(&before);
    NVIC_DisableIRQ(RTC_WKUP_IRQn);
    ped_stream_clear_wutf();
    EXTI->PR1 = EXTI_PR1_PIF2;

    start_ms = timebase_millis();
    while (!timebase_elapsed(start_ms, 2500U)) {
        if ((RTC->ISR & RTC_ISR_WUTF) != 0UL) {
            wutf_seen = 1U;
        }
        if ((GPIOB->IDR & (1UL << 2U)) != 0UL) {
            pb2_high = 1U;
        }
        if ((EXTI->PR1 & EXTI_PR1_PIF2) != 0UL) {
            exti2_pending = 1U;
        }
    }

    (void)board_ped_get_stats(&after);
    ped_stream_log_u32("wutf_seen", wutf_seen);
    ped_stream_log_u32("pb2_high_seen", pb2_high);
    ped_stream_log_u32("exti2_pending_seen", exti2_pending);
    ped_stream_log_u32("seconds_marked_delta", after.seconds_marked - before.seconds_marked);
    ped_stream_log_hex("RTC_ISR_now", RTC->ISR);
    ped_stream_log_hex("GPIOB_IDR_now", GPIOB->IDR);

    ped_stream_clear_wutf();
    NVIC_EnableIRQ(RTC_WKUP_IRQn);

    debug_log_write("DIAG software EXTI2\r\n");
    (void)board_ped_get_stats(&before);
    EXTI->SWIER1 = EXTI_SWIER1_SWI2;
    timebase_delay_ms_blocking(10U);
    (void)board_ped_get_stats(&after);
    ped_stream_log_u32("software_exti2_marks", after.seconds_marked - before.seconds_marked);

    debug_log_write("DIAG re-init PED and restart acquisition\r\n");
    ped_stream_log_u32("ped_reg_init", (uint32_t)board_ped_reg_init());
    ped_stream_log_u32("acquisition_start", (uint32_t)board_ped_acquisition_start());
    ped_stream_dump_exti("after re-init");
    ped_stream_log_u32("marks_in_3500ms_after_reinit", ped_stream_count_marks(3500U));
    ped_stream_dump_exti("3500ms after re-init");
}

#if (PED_STREAM_WITH_PED != 0)
static void ped_stream_start_ped(void) {
    BoardStatus status;
    uint32_t ped_status = 0U;

    status = board_ped_power_on();
    ped_stream_log_u32("ped_power_on", (uint32_t)status);
    if (status != BOARD_OK) {
        ped_stream_fail("PED_POWER_ON", (uint32_t)status);
    }

    status = board_ped_read_status(&ped_status);
    ped_stream_log_u32("ped_status", ped_status);

    status = board_ped_write_register(PED_STREAM_ADDR_CONF_TRIG, PED_STREAM_CONF_TRIG);
    ped_stream_log_u32("write_conf_trig", (uint32_t)status);

    status = board_ped_write_register(PED_STREAM_ADDR_THRESHOLD, PED_STREAM_THRESHOLD);
    ped_stream_log_u32("write_threshold", (uint32_t)status);

    (void)board_ped_set_sleep(0U);
    (void)board_ped_set_inhibit(0U);
}
#endif

static void ped_stream_log_second(const BoardPedRecord *record, uint32_t events,
                                  uint32_t period_ms) {
    BoardPedStats stats;
    uint32_t faults = 0U;

    (void)board_ped_get_stats(&stats);
    (void)board_ped_take_faults(&faults);

    debug_log_write("second rtc=");
    debug_log_write_u32_inline(record->rtc_seconds);
    debug_log_write(" dt_ms=");
    debug_log_write_u32_inline(period_ms);
    debug_log_write(" flags=");
    debug_log_write_u32_inline(record->flags);
    debug_log_write(" events=");
    debug_log_write_u32_inline(events);
    debug_log_write(" n_d=");
    debug_log_write_u32_inline(record->data[0]);
    debug_log_write(" n_trig=");
    debug_log_write_u32_inline(record->data[3]);
    debug_log_write(" held=");
    debug_log_write_u32_inline(stats.events_held);
    debug_log_write(" high=");
    debug_log_write_u32_inline(stats.ring_high_water);
    debug_log_write(" lost=");
    debug_log_write_u32_inline(stats.seconds_lost);
    debug_log_write(" faults=");
    debug_log_write_u32_inline(faults);
    debug_log_write("\r\n");
}

int main(void) {
    BoardStatus status;
    size_t count;
    size_t i;
    uint32_t seconds_seen = 0U;
    uint32_t bad_periods = 0U;
    uint32_t bad_times = 0U;
    uint32_t events_this_second = 0U;
    uint32_t last_mark_ms = 0U;
    uint32_t last_rtc = 0U;
    uint32_t wait_start_ms;
    uint32_t now_ms;
    uint32_t period_ms;
    uint8_t verdict_logged = 0U;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    debug_log_write("\r\nPED STREAM TEST\r\n");
    ped_stream_log_u32("with_ped", (uint32_t)PED_STREAM_WITH_PED);

    ped_stream_log_hex("RCC_CSR_reset_flags", RCC->CSR);
    RCC->CSR |= RCC_CSR_RMVF;

    status = board_init_hardware();
    ped_stream_log_u32("board_init_hardware", (uint32_t)status);
    if (status != BOARD_OK) {
        ped_stream_fail("INIT", (uint32_t)status);
    }

    RTC->BKP0R = RTC->BKP0R + 1U;
    ped_stream_log_u32("boot_counter_bkp0", RTC->BKP0R);
    ped_stream_dump_exti("after board_init_hardware");

#if (PED_STREAM_WITH_PED != 0)
    ped_stream_start_ped();
#endif

    status = board_ped_acquisition_start();
    ped_stream_log_u32("acquisition_start", (uint32_t)status);
    ped_stream_dump_exti("after acquisition_start");

    wait_start_ms = timebase_millis();

    while (1) {
        status = board_ped_take_records(ped_stream_records, PED_STREAM_TAKE_CAPACITY, &count);
        if (status != BOARD_OK) {
            ped_stream_fail("TAKE_RECORDS", (uint32_t)status);
        }

        now_ms = timebase_millis();

        for (i = 0U; i < count; ++i) {
            if (ped_stream_records[i].kind == BOARD_PED_RECORD_EVENT) {
                ++events_this_second;
                continue;
            }

            period_ms = (seconds_seen == 0U) ? 0U : (now_ms - last_mark_ms);

            if (seconds_seen > 0U) {
                if ((period_ms < PED_STREAM_PERIOD_MIN_MS) || (period_ms > PED_STREAM_PERIOD_MAX_MS)) {
                    ++bad_periods;
                }
                if (ped_stream_records[i].rtc_seconds != (last_rtc + 1U)) {
                    ++bad_times;
                }
            }

            ped_stream_log_second(&ped_stream_records[i], events_this_second, period_ms);

            last_mark_ms = now_ms;
            last_rtc = ped_stream_records[i].rtc_seconds;
            events_this_second = 0U;
            ++seconds_seen;
        }

        if ((seconds_seen == 0U) && timebase_elapsed(wait_start_ms, PED_STREAM_TIMEOUT_MS)) {
            ped_stream_diagnose();
            ped_stream_fail("NO_SECOND_ON_PB2", 0U);
        }

        if ((seconds_seen > 0U) && timebase_elapsed(last_mark_ms, PED_STREAM_TIMEOUT_MS)) {
            ped_stream_fail("SECOND_STOPPED", seconds_seen);
        }

        if ((verdict_logged == 0U) && (seconds_seen >= (PED_STREAM_CHECK_SECONDS + 1U))) {
            ped_stream_log_u32("bad_periods", bad_periods);
            ped_stream_log_u32("bad_times", bad_times);
            debug_log_write(((bad_periods == 0U) && (bad_times == 0U)) ?
                            "PED STREAM SECONDS PASS\r\n" : "PED STREAM SECONDS FAIL\r\n");
            verdict_logged = 1U;
        }
    }

    return 0;
}
