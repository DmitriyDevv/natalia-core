#include "rtc_private.h"

#include <stdint.h>

#include "gpio.h"
#include "stm32l496xx.h"
#include "timebase.h"

#define RTC_LSE_STARTUP_TIMEOUT_MS (5000UL)

static BoardStatus rtc_unlock_backup_domain(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;

    PWR->CR1 |= PWR_CR1_DBP;

    if ((PWR->CR1 & PWR_CR1_DBP) == 0U) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static void rtc_disable_write_protection(void)
{
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
}

static void rtc_enable_write_protection(void)
{
    RTC->WPR = 0xFFU;
}

static BoardStatus rtc_enable_lse(void)
{
    uint32_t start_ms;

    RCC->BDCR |= RCC_BDCR_LSEON;

    start_ms = timebase_millis();

    while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0U) {
        if (timebase_elapsed(start_ms, RTC_LSE_STARTUP_TIMEOUT_MS)) {
            return BOARD_ERR_TIMEOUT;
        }
    }

    return BOARD_OK;
}

static BoardStatus rtc_select_lse_clock(void)
{
    uint32_t current_source;
    BoardStatus status;

    status = rtc_enable_lse();
    if (status != BOARD_OK) {
        return status;
    }

    current_source = RCC->BDCR & RCC_BDCR_RTCSEL;


    if (current_source == 0U) {
        RCC->BDCR &= ~RCC_BDCR_RTCSEL;
        RCC->BDCR |= RCC_BDCR_RTCSEL_0;
    } else if (current_source != RCC_BDCR_RTCSEL_0) {
        return BOARD_ERR_NOT_READY;
    } else {
        /* LSE already selected. */
    }

    RCC->BDCR |= RCC_BDCR_RTCEN;

    return BOARD_OK;
}

static BoardStatus rtc_configure_prescalers(void)
{
    uint32_t prer;

    rtc_disable_write_protection();

    RTC->ISR |= RTC_ISR_INIT;

    while ((RTC->ISR & RTC_ISR_INITF) == 0U) {
    }

    prer = (RTC_PREDIV_A_VALUE << RTC_PRER_PREDIV_A_Pos) |
           (RTC_PREDIV_S_VALUE << RTC_PRER_PREDIV_S_Pos);

    RTC->PRER = prer;


    RTC->CR &= ~RTC_CR_FMT;


    RTC->CR |= RTC_CR_BYPSHAD;

    RTC->ISR &= ~RTC_ISR_INIT;

    rtc_enable_write_protection();

    return BOARD_OK;
}

static BoardStatus rtc_configure_output_pin(void)
{
    const GpioConfig rtc_out_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    return gpio_configure(BOARD_PIN_RTC_OUT, &rtc_out_config);
}

static BoardStatus rtc_enable_calibration_output_1hz(void)
{
    rtc_disable_write_protection();


    RTC->CR &= ~RTC_CR_POL;
    RTC->CR |= RTC_CR_COSEL;
    RTC->CR |= RTC_CR_COE;

    rtc_enable_write_protection();

    return BOARD_OK;
}

static BoardStatus rtc_configure_wakeup_1hz_interrupt(void)
{
    rtc_disable_write_protection();


    RTC->CR &= ~RTC_CR_WUTE;

    while ((RTC->ISR & RTC_ISR_WUTWF) == 0U) {
    }


    RTC->CR &= ~RTC_CR_WUCKSEL;
    RTC->CR |= RTC_CR_WUCKSEL_2;

    RTC->WUTR = 0U;

    RTC->ISR &= ~RTC_ISR_WUTF;

    RTC->CR |= RTC_CR_WUTIE;
    RTC->CR |= RTC_CR_WUTE;

    rtc_enable_write_protection();


    EXTI->IMR1 |= EXTI_IMR1_IM20;
    EXTI->RTSR1 |= EXTI_RTSR1_RT20;
    EXTI->PR1 = EXTI_PR1_PIF20;

    NVIC_ClearPendingIRQ(RTC_WKUP_IRQn);
    NVIC_SetPriority(RTC_WKUP_IRQn, 5U);
    NVIC_EnableIRQ(RTC_WKUP_IRQn);

    return BOARD_OK;
}

BoardStatus rtc_configure_hardware(void)
{
    BoardStatus status;

    status = rtc_unlock_backup_domain();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_select_lse_clock();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_configure_output_pin();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_configure_prescalers();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_enable_calibration_output_1hz();
    if (status != BOARD_OK) {
        return status;
    }

    return rtc_configure_wakeup_1hz_interrupt();
}