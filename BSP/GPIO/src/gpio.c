#include "gpio.h"

#include <stdbool.h>
#include <stddef.h>

#include "stm32l496xx.h"

_Static_assert(GPIO_MODE_INPUT == 0, "GPIO mode encoding mismatch");
_Static_assert(GPIO_MODE_OUTPUT == 1, "GPIO mode encoding mismatch");
_Static_assert(GPIO_MODE_ALTERNATE == 2, "GPIO mode encoding mismatch");
_Static_assert(GPIO_MODE_ANALOG == 3, "GPIO mode encoding mismatch");

static bool gpio_is_valid_mode(GpioMode mode) {
    return (mode == GPIO_MODE_INPUT) ||
        (mode == GPIO_MODE_OUTPUT) ||
        (mode == GPIO_MODE_ALTERNATE) ||
        (mode == GPIO_MODE_ANALOG);
}

static bool gpio_is_valid_pull(GpioPull pull) {
    return (pull == GPIO_PULL_NONE) ||
        (pull == GPIO_PULL_UP) ||
        (pull == GPIO_PULL_DOWN);
}

static bool gpio_is_valid_output_type(GpioOutputType output_type) {
    return (output_type == GPIO_OUTPUT_PUSH_PULL) ||
        (output_type == GPIO_OUTPUT_OPEN_DRAIN);
}

static bool gpio_is_valid_speed(GpioSpeed speed) {
    return (speed == GPIO_SPEED_LOW) ||
        (speed == GPIO_SPEED_MEDIUM) ||
        (speed == GPIO_SPEED_HIGH) ||
        (speed == GPIO_SPEED_VERY_HIGH);
}

static bool gpio_is_valid_level(GpioLevel level) {
    return (level == GPIO_LEVEL_LOW) ||
        (level == GPIO_LEVEL_HIGH);
}

static BoardStatus gpio_get_pin(BoardPinId id, const BoardPinDesc** pin) {
    const BoardPinDesc* result;

    if (pin == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    result = board_pin_get(id);
    if (result == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (result->port == BOARD_PIN_UNUSED_PORT ||
        result->pin == BOARD_PIN_UNUSED_NUMBER) {
        return BOARD_ERR_UNSUPPORTED;
    }

    if (result->pin > 15U) {
        return BOARD_ERR_INVALID_ARG;
    }

    *pin = result;

    return BOARD_OK;
}

static BoardStatus gpio_enable_supply_domain(const BoardPinDesc* pin) {
    if (pin == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (pin->port == GPIOG && pin->pin >= 2U) {
        RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
        (void)RCC->APB1ENR1;

        PWR->CR2 |= PWR_CR2_IOSV;
        (void)PWR->CR2;
    }

    return BOARD_OK;
}

static void gpio_write_latch(const BoardPinDesc* pin, GpioLevel level) {
    const uint32_t mask = 1UL << pin->pin;

    if (level == GPIO_LEVEL_HIGH) {
        pin->port->BSRR = mask;
    } else {
        pin->port->BSRR = mask << 16U;
    }
}

BoardStatus gpio_enable_port_clock(GPIO_TypeDef* port) {
    uint32_t enable_mask;

    if (port == GPIOA) {
        enable_mask = RCC_AHB2ENR_GPIOAEN;
    } else if (port == GPIOB) {
        enable_mask = RCC_AHB2ENR_GPIOBEN;
    } else if (port == GPIOC) {
        enable_mask = RCC_AHB2ENR_GPIOCEN;
    } else if (port == GPIOD) {
        enable_mask = RCC_AHB2ENR_GPIODEN;
    } else if (port == GPIOE) {
        enable_mask = RCC_AHB2ENR_GPIOEEN;
    } else if (port == GPIOF) {
        enable_mask = RCC_AHB2ENR_GPIOFEN;
    } else if (port == GPIOG) {
        enable_mask = RCC_AHB2ENR_GPIOGEN;
    } else {
        return BOARD_ERR_UNSUPPORTED;
    }

    RCC->AHB2ENR |= enable_mask;
    (void)RCC->AHB2ENR;

    return BOARD_OK;
}

BoardStatus gpio_configure(BoardPinId id, const GpioConfig* config) {
    const BoardPinDesc* pin;
    uint32_t position;
    uint32_t mode_shift;
    uint32_t bit_mask;
    uint32_t two_bit_mask;
    uint32_t afr_index;
    uint32_t afr_shift;
    BoardStatus status;

    if (config == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!gpio_is_valid_mode(config->mode) ||
        !gpio_is_valid_pull(config->pull) ||
        !gpio_is_valid_output_type(config->output_type) ||
        !gpio_is_valid_speed(config->speed) ||
        !gpio_is_valid_level(config->initial_level)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_get_pin(id, &pin);
    if (status != BOARD_OK) {
        return status;
    }

    if ((config->mode == GPIO_MODE_ALTERNATE) && (pin->af > 15U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_enable_supply_domain(pin);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_enable_port_clock(pin->port);
    if (status != BOARD_OK) {
        return status;
    }

    position = (uint32_t)pin->pin;
    mode_shift = position * 2U;
    bit_mask = 1UL << position;
    two_bit_mask = 3UL << mode_shift;

    if (config->mode == GPIO_MODE_OUTPUT) {
        gpio_write_latch(pin, config->initial_level);
    }

    pin->port->OTYPER &= ~bit_mask;
    pin->port->OTYPER |= ((uint32_t)config->output_type << position);

    pin->port->OSPEEDR &= ~two_bit_mask;
    pin->port->OSPEEDR |= ((uint32_t)config->speed << mode_shift);

    pin->port->PUPDR &= ~two_bit_mask;
    pin->port->PUPDR |= ((uint32_t)config->pull << mode_shift);

    if (config->mode == GPIO_MODE_ALTERNATE) {
        afr_index = position / 8U;
        afr_shift = (position % 8U) * 4U;

        pin->port->AFR[afr_index] &= ~(0xFUL << afr_shift);
        pin->port->AFR[afr_index] |= ((uint32_t)pin->af << afr_shift);
    }

    pin->port->MODER &= ~two_bit_mask;
    pin->port->MODER |= ((uint32_t)config->mode << mode_shift);

    return BOARD_OK;
}

BoardStatus gpio_write(BoardPinId id, GpioLevel level) {
    const BoardPinDesc* pin;
    BoardStatus status;

    if (!gpio_is_valid_level(level)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_get_pin(id, &pin);
    if (status != BOARD_OK) {
        return status;
    }

    gpio_write_latch(pin, level);

    return BOARD_OK;
}

BoardStatus gpio_read(BoardPinId id, GpioLevel* level) {
    const BoardPinDesc* pin;
    uint32_t mask;
    BoardStatus status;

    if (level == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_get_pin(id, &pin);
    if (status != BOARD_OK) {
        return status;
    }

    mask = 1UL << pin->pin;

    *level = ((pin->port->IDR & mask) != 0U)
                 ? GPIO_LEVEL_HIGH
                 : GPIO_LEVEL_LOW;

    return BOARD_OK;
}

BoardStatus gpio_read_output_latch(BoardPinId id, GpioLevel* level) {
    const BoardPinDesc* pin;
    uint32_t mask;
    BoardStatus status;

    if (level == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = gpio_get_pin(id, &pin);
    if (status != BOARD_OK) {
        return status;
    }

    mask = 1UL << pin->pin;

    *level = ((pin->port->ODR & mask) != 0U)
                 ? GPIO_LEVEL_HIGH
                 : GPIO_LEVEL_LOW;

    return BOARD_OK;
}

#define GPIO_EXTI_SUPPORTED_LINES ((1UL << 1U) | (1UL << 5U) | (1UL << 6U) | \
                                   (1UL << 7U) | (1UL << 8U) | (1UL << 9U))

static volatile uint32_t gpio_exti_latched = 0U;

void EXTI1_IRQHandler(void);
void EXTI9_5_IRQHandler(void);

static void gpio_exti_handle(uint32_t lines) {
    uint32_t pending = EXTI->PR1 & lines & EXTI->IMR1;

    if (pending != 0U) {
        EXTI->PR1 = pending;
        gpio_exti_latched |= pending;
    }
}

void EXTI1_IRQHandler(void) {
    gpio_exti_handle(1UL << 1U);
}

void EXTI9_5_IRQHandler(void) {
    gpio_exti_handle((1UL << 5U) | (1UL << 6U) | (1UL << 7U) | (1UL << 8U) | (1UL << 9U));
}

static BoardStatus gpio_exti_port_index(const GPIO_TypeDef* port, uint32_t* index) {
    if (port == GPIOA) {
        *index = 0U;
    } else if (port == GPIOB) {
        *index = 1U;
    } else if (port == GPIOC) {
        *index = 2U;
    } else if (port == GPIOD) {
        *index = 3U;
    } else if (port == GPIOE) {
        *index = 4U;
    } else if (port == GPIOF) {
        *index = 5U;
    } else if (port == GPIOG) {
        *index = 6U;
    } else {
        return BOARD_ERR_UNSUPPORTED;
    }

    return BOARD_OK;
}

static BoardStatus gpio_exti_line(BoardPinId id, const BoardPinDesc** pin, uint32_t* line_mask) {
    BoardStatus status;

    status = gpio_get_pin(id, pin);
    if (status != BOARD_OK) {
        return status;
    }

    *line_mask = 1UL << (*pin)->pin;

    if ((*line_mask & GPIO_EXTI_SUPPORTED_LINES) == 0U) {
        return BOARD_ERR_UNSUPPORTED;
    }

    return BOARD_OK;
}

static IRQn_Type gpio_exti_irqn(uint32_t line_mask) {
    return (line_mask == (1UL << 1U)) ? EXTI1_IRQn : EXTI9_5_IRQn;
}

BoardStatus gpio_falling_edge_irq_enable(BoardPinId id) {
    const BoardPinDesc* pin;
    uint32_t line_mask;
    uint32_t port_index;
    uint32_t exticr_index;
    uint32_t exticr_shift;
    uint32_t primask;
    BoardStatus status;

    status = gpio_exti_line(id, &pin, &line_mask);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_exti_port_index(pin->port, &port_index);
    if (status != BOARD_OK) {
        return status;
    }

    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    exticr_index = (uint32_t)pin->pin / 4U;
    exticr_shift = ((uint32_t)pin->pin % 4U) * 4U;

    EXTI->IMR1 &= ~line_mask;

    SYSCFG->EXTICR[exticr_index] &= ~(0xFUL << exticr_shift);
    SYSCFG->EXTICR[exticr_index] |= port_index << exticr_shift;

    EXTI->RTSR1 &= ~line_mask;
    EXTI->FTSR1 |= line_mask;
    EXTI->PR1 = line_mask;

    primask = __get_PRIMASK();
    __disable_irq();
    gpio_exti_latched &= ~line_mask;
    __set_PRIMASK(primask);

    EXTI->IMR1 |= line_mask;

    NVIC_ClearPendingIRQ(gpio_exti_irqn(line_mask));
    NVIC_EnableIRQ(gpio_exti_irqn(line_mask));

    return BOARD_OK;
}

BoardStatus gpio_falling_edge_irq_disable(BoardPinId id) {
    const BoardPinDesc* pin;
    uint32_t line_mask;
    uint32_t primask;
    BoardStatus status;

    status = gpio_exti_line(id, &pin, &line_mask);
    if (status != BOARD_OK) {
        return status;
    }

    EXTI->IMR1 &= ~line_mask;
    EXTI->FTSR1 &= ~line_mask;
    EXTI->PR1 = line_mask;

    primask = __get_PRIMASK();
    __disable_irq();
    gpio_exti_latched &= ~line_mask;
    __set_PRIMASK(primask);

    return BOARD_OK;
}

BoardStatus gpio_falling_edge_irq_take(BoardPinId id, uint8_t *triggered) {
    const BoardPinDesc* pin;
    uint32_t line_mask;
    uint32_t primask;
    BoardStatus status;

    if (triggered == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *triggered = 0U;

    status = gpio_exti_line(id, &pin, &line_mask);
    if (status != BOARD_OK) {
        return status;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if ((gpio_exti_latched & line_mask) != 0U) {
        gpio_exti_latched &= ~line_mask;
        *triggered = 1U;
    }
    __set_PRIMASK(primask);

    return BOARD_OK;
}

BoardStatus gpio_set_disconnected(BoardPinId id) {
    static const GpioConfig disconnected_config = {
        .mode = GPIO_MODE_ANALOG,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    return gpio_configure(id, &disconnected_config);
}
