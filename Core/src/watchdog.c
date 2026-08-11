#include "watchdog.h"

#if defined(NATALIA_ENABLE_WATCHDOG) && (NATALIA_ENABLE_WATCHDOG != 0)

#include "stm32l496xx.h"

#define WATCHDOG_KEY_ENABLE (0x00005555UL)
#define WATCHDOG_KEY_RELOAD (0x0000AAAAUL)
#define WATCHDOG_KEY_START  (0x0000CCCCUL)

/* /256 prescaler -> ~8 ms per tick at the ~32 kHz LSI; reload 500 -> ~4 s. */
#define WATCHDOG_PRESCALER  (6UL)
#define WATCHDOG_RELOAD     (500UL)

#define WATCHDOG_SR_GUARD   (100000UL)

BoardStatus watchdog_init(void) {
    uint32_t guard;

    DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;

    IWDG->KR = WATCHDOG_KEY_START;
    IWDG->KR = WATCHDOG_KEY_ENABLE;
    IWDG->PR = WATCHDOG_PRESCALER;
    IWDG->RLR = WATCHDOG_RELOAD;

    guard = 0U;
    while (IWDG->SR != 0U) {
        ++guard;
        if (guard >= WATCHDOG_SR_GUARD) {
            return BOARD_ERR_TIMEOUT;
        }
    }

    IWDG->KR = WATCHDOG_KEY_RELOAD;

    return BOARD_OK;
}

void watchdog_kick(void) {
    IWDG->KR = WATCHDOG_KEY_RELOAD;
}

#else /* watchdog disabled */

BoardStatus watchdog_init(void) {
    return BOARD_OK;
}

void watchdog_kick(void) {
}

#endif /* NATALIA_ENABLE_WATCHDOG */
