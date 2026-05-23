#include "clock.h"

#include "board_config.h"
#include "stm32l496xx.h"
#include "system_stm32l4xx.h"

#define CLOCK_WAIT_TIMEOUT_ITERATIONS (1000000UL)

/*
 * PLL target configuration:
 *
 * Output:
 *   PLLR output = 80 MHz
 *
 * HSI16 development configuration:
 *   PLL input = HSI16 / 2 = 8 MHz
 *   VCO       = 8 MHz * 20 = 160 MHz
 *   PLLR      = 160 MHz / 2 = 80 MHz
 *
 * HSE target-board configuration:
 *   PLL input = HSE / 1 = 8 MHz
 *   VCO       = 8 MHz * 20 = 160 MHz
 *   PLLR      = 160 MHz / 2 = 80 MHz
 */
#define CLOCK_PLLN_REGISTER_VALUE (20UL)
#define CLOCK_PLLR_REGISTER_VALUE (0UL) /* PLLR = /2 */

#if (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSI16)
#define CLOCK_PLLM_REGISTER_VALUE (1UL) /* PLLM = /2 */
#define CLOCK_PLL_SOURCE_BITS     RCC_PLLCFGR_PLLSRC_HSI
#elif ((BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSE_CRYSTAL) || \
       (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSE_BYPASS))
#define CLOCK_PLLM_REGISTER_VALUE (0UL) /* PLLM = /1 */
#define CLOCK_PLL_SOURCE_BITS     RCC_PLLCFGR_PLLSRC_HSE
#else
#error "Unsupported BOARD_CLOCK_SOURCE"
#endif

_Static_assert(BOARD_SYSCLK_HZ == 80000000UL,
               "Clock configuration expects 80 MHz SYSCLK");

static BoardStatus clock_wait_flag_set(volatile uint32_t *reg,
                                       uint32_t mask)
{
    uint32_t timeout = CLOCK_WAIT_TIMEOUT_ITERATIONS;

    while ((*reg & mask) == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus clock_wait_flag_clear(volatile uint32_t *reg,
                                         uint32_t mask)
{
    uint32_t timeout = CLOCK_WAIT_TIMEOUT_ITERATIONS;

    while ((*reg & mask) != 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus clock_configure_voltage_scaling(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;


    PWR->CR1 &= ~PWR_CR1_VOS;
    PWR->CR1 |= PWR_CR1_VOS_0;

    return clock_wait_flag_clear(&PWR->SR2, PWR_SR2_VOSF);
}

static BoardStatus clock_configure_flash(void)
{
    FLASH->ACR = FLASH_ACR_ICEN |
                 FLASH_ACR_DCEN |
                 FLASH_ACR_PRFTEN |
                 FLASH_ACR_LATENCY_4WS;

    if ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus clock_enable_pll_source(void)
{
#if (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSI16)

    RCC->CR |= RCC_CR_HSION;

    return clock_wait_flag_set(&RCC->CR, RCC_CR_HSIRDY);

#elif (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSE_CRYSTAL)

    RCC->CR &= ~RCC_CR_HSEON;

    if (clock_wait_flag_clear(&RCC->CR, RCC_CR_HSERDY) != BOARD_OK) {
        return BOARD_ERR_TIMEOUT;
    }

    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;

    return clock_wait_flag_set(&RCC->CR, RCC_CR_HSERDY);

#elif (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSE_BYPASS)

    RCC->CR &= ~RCC_CR_HSEON;

    if (clock_wait_flag_clear(&RCC->CR, RCC_CR_HSERDY) != BOARD_OK) {
        return BOARD_ERR_TIMEOUT;
    }

    RCC->CR |= RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;

    return clock_wait_flag_set(&RCC->CR, RCC_CR_HSERDY);

#else
#error "Unsupported BOARD_CLOCK_SOURCE"
#endif
}

static BoardStatus clock_configure_pll(void)
{
    BoardStatus status;

    RCC->CR &= ~RCC_CR_PLLON;

    status = clock_wait_flag_clear(&RCC->CR, RCC_CR_PLLRDY);
    if (status != BOARD_OK) {
        return status;
    }

    RCC->PLLCFGR =
        CLOCK_PLL_SOURCE_BITS |
        (CLOCK_PLLM_REGISTER_VALUE << RCC_PLLCFGR_PLLM_Pos) |
        (CLOCK_PLLN_REGISTER_VALUE << RCC_PLLCFGR_PLLN_Pos) |
        (CLOCK_PLLR_REGISTER_VALUE << RCC_PLLCFGR_PLLR_Pos) |
        RCC_PLLCFGR_PLLREN;

    RCC->CR |= RCC_CR_PLLON;

    return clock_wait_flag_set(&RCC->CR, RCC_CR_PLLRDY);
}

static BoardStatus clock_switch_system_clock_to_pll(void)
{
    uint32_t timeout = CLOCK_WAIT_TIMEOUT_ITERATIONS;


    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2);

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    SystemCoreClockUpdate();

    if (SystemCoreClock != BOARD_SYSCLK_HZ) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

BoardStatus clock_init(void)
{
    BoardStatus status;

    status = clock_configure_voltage_scaling();
    if (status != BOARD_OK) {
        return status;
    }

    status = clock_configure_flash();
    if (status != BOARD_OK) {
        return status;
    }

    status = clock_enable_pll_source();
    if (status != BOARD_OK) {
        return status;
    }

    status = clock_configure_pll();
    if (status != BOARD_OK) {
        return status;
    }

    return clock_switch_system_clock_to_pll();
}

uint32_t clock_get_sysclk_hz(void)
{
    return BOARD_SYSCLK_HZ;
}

uint32_t clock_get_hclk_hz(void)
{
    return BOARD_HCLK_HZ;
}

uint32_t clock_get_pclk1_hz(void)
{
    return BOARD_PCLK1_HZ;
}

uint32_t clock_get_pclk2_hz(void)
{
    return BOARD_PCLK2_HZ;
}