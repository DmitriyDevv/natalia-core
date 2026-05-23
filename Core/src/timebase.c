#include "timebase.h"

#include "stm32l496xx.h"

#define TIMEBASE_TICK_HZ (1000UL)

static volatile uint32_t timebase_ticks_ms = 0U;


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

void SysTick_Handler(void) {
    ++timebase_ticks_ms;
}
