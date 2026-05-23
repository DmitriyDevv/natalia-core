#include "lpuart1.h"

#include "gpio.h"
#include "stm32l496xx.h"

#define LPUART1_KERNEL_CLOCK_HZ (16000000UL)
#define LPUART1_BRR_MULTIPLIER  (256ULL)

static BoardStatus lpuart1_enable_hsi16(void) {
    RCC->CR |= RCC_CR_HSION;

    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {}

    return BOARD_OK;
}

static BoardStatus lpuart1_configure_clock(void) {
    BoardStatus status;

    status = lpuart1_enable_hsi16();
    if (status != BOARD_OK) {
        return status;
    }

    RCC->CCIPR &= ~RCC_CCIPR_LPUART1SEL;
    RCC->CCIPR |= RCC_CCIPR_LPUART1SEL_1;

    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC->APB1ENR2;

    RCC->APB1RSTR2 |= RCC_APB1RSTR2_LPUART1RST;
    RCC->APB1RSTR2 &= ~RCC_APB1RSTR2_LPUART1RST;

    return BOARD_OK;
}

static BoardStatus lpuart1_configure_pins(void) {
    const GpioConfig tx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_UP,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    const GpioConfig rx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_UP,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_HIGH
    };

    BoardStatus status;

    status = gpio_configure(BOARD_PIN_DEBUG_UART_TX, &tx_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_DEBUG_UART_RX, &rx_config);
}

static uint32_t lpuart1_calculate_brr(uint32_t baudrate) {
    const uint64_t numerator =
        (uint64_t)LPUART1_KERNEL_CLOCK_HZ * LPUART1_BRR_MULTIPLIER +
        (uint64_t)baudrate / 2ULL;

    return (uint32_t)(numerator / (uint64_t)baudrate);
}

BoardStatus lpuart1_init(uint32_t baudrate) {
    uint32_t brr;
    BoardStatus status;

    if (baudrate == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = lpuart1_configure_clock();
    if (status != BOARD_OK) {
        return status;
    }

    status = lpuart1_configure_pins();
    if (status != BOARD_OK) {
        return status;
    }

    brr = lpuart1_calculate_brr(baudrate);

    if ((brr < 0x300UL) || (brr > 0xFFFFFUL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    LPUART1->CR1 &= ~USART_CR1_UE;

    LPUART1->CR1 = 0U;
    LPUART1->CR2 = 0U;
    LPUART1->CR3 = 0U;
    LPUART1->BRR = brr;

    LPUART1->CR1 = USART_CR1_TE | USART_CR1_RE;
    LPUART1->CR1 |= USART_CR1_UE;

    return BOARD_OK;
}
