#include "timebase.h"

#include "stm32l496xx.h"

#define TIMEBASE_TICK_HZ (1000UL)

static volatile uint32_t timebase_ticks_ms = 0U;
static uint32_t timebase_cycles_per_us = 1U;


void SysTick_Handler(void);

BoardStatus timebase_init(void) {
    uint32_t reload_ticks;

    SystemCoreClockUpdate();

    if (SystemCoreClock < TIMEBASE_TICK_HZ) {
        return BOARD_ERR_INVALID_ARG;
    }

    reload_ticks = SystemCoreClock / TIMEBASE_TICK_HZ;

    if ((reload_ticks == 0U) ||
        (reload_ticks > (SysTick_LOAD_RELOAD_Msk + 1UL))) {
        return BOARD_ERR_INVALID_ARG;
    }

    timebase_ticks_ms = 0U;

    timebase_cycles_per_us = SystemCoreClock / 1000000UL;
    if (timebase_cycles_per_us == 0U) {
        timebase_cycles_per_us = 1U;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if (SysTick_Config(reload_ticks) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

uint32_t timebase_millis(void) {
    return timebase_ticks_ms;
}

bool timebase_elapsed(uint32_t start_ms, uint32_t interval_ms) {
    const uint32_t elapsed_ms = timebase_millis() - start_ms;

    return elapsed_ms >= interval_ms;
}

void timebase_delay_ms_blocking(uint32_t delay_ms) {
    const uint32_t start_ms = timebase_millis();

    while (!timebase_elapsed(start_ms, delay_ms)) {
        __NOP();
    }
}

void timebase_delay_us_blocking(uint32_t delay_us) {
    uint32_t start_cycles;
    uint32_t target_cycles;
    volatile uint32_t spin;

    if (delay_us == 0U) {
        return;
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        for (spin = 0U; spin < (delay_us * timebase_cycles_per_us); ++spin) {
            __NOP();
        }

        return;
    }

    start_cycles = DWT->CYCCNT;
    target_cycles = delay_us * timebase_cycles_per_us;

    while ((DWT->CYCCNT - start_cycles) < target_cycles) {
        __NOP();
    }
}

uint32_t timebase_cycles(void) {
    return DWT->CYCCNT;
}

uint32_t timebase_us_since(uint32_t start_cycles) {
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        return UINT32_MAX;
    }

    return (DWT->CYCCNT - start_cycles) / timebase_cycles_per_us;
}

__attribute__((weak)) void timebase_tick_hook(void) {
}

void SysTick_Handler(void) {
    ++timebase_ticks_ms;
    timebase_tick_hook();
}
