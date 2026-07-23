#include "usart2.h"

#include "gpio.h"
#include "stm32l496xx.h"

#define USART2_KERNEL_CLOCK_HZ (16000000UL)

static BoardStatus usart2_enable_hsi16(void) {
    RCC->CR |= RCC_CR_HSION;

    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {}

    return BOARD_OK;
}

static BoardStatus usart2_configure_clock(void) {
    BoardStatus status;

    status = usart2_enable_hsi16();
    if (status != BOARD_OK) {
        return status;
    }

    RCC->CCIPR &= ~RCC_CCIPR_USART2SEL;
    RCC->CCIPR |= RCC_CCIPR_USART2SEL_1;

    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    (void)RCC->APB1ENR1;

    RCC->APB1RSTR1 |= RCC_APB1RSTR1_USART2RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_USART2RST;

    return BOARD_OK;
}

static BoardStatus usart2_configure_pins(void) {
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

    status = gpio_configure(BOARD_PIN_USART2_TX, &tx_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_USART2_RX, &rx_config);
}

static uint32_t usart2_calculate_brr(uint32_t baudrate) {
    return (USART2_KERNEL_CLOCK_HZ + (baudrate / 2U)) / baudrate;
}

BoardStatus usart2_init(uint32_t baudrate) {
    uint32_t brr;
    BoardStatus status;

    if (baudrate == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = usart2_configure_clock();
    if (status != BOARD_OK) {
        return status;
    }

    status = usart2_configure_pins();
    if (status != BOARD_OK) {
        return status;
    }

    brr = usart2_calculate_brr(baudrate);

    if ((brr < 0x10UL) || (brr > 0xFFFFUL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    USART2->CR1 &= ~USART_CR1_UE;

    USART2->CR1 = 0U;
    USART2->CR2 = 0U;
    USART2->CR3 = 0U;
    USART2->BRR = brr;

    USART2->CR1 = USART_CR1_TE | USART_CR1_RE;
    USART2->CR1 |= USART_CR1_UE;

    return BOARD_OK;
}
