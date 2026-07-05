#include "ped_reg.h"

#include <stdbool.h>
#include <string.h>

#include "stm32l4xx.h"

#define PED_REG_READY_PIN      (1UL << 0U)
#define PED_REG_DIR_PIN        (1UL << 8U)
#define PED_REG_WRCLK_PIN      (1UL << 9U)
#define PED_REG_ADDR_MASK      (0x00FFUL)

#define PED_REG_POWER_PIN      (1UL << 6U)
#define PED_REG_TGRES_PIN      (1UL << 7U)
#define PED_REG_INHIBIT_PIN    (1UL << 8U)
#define PED_REG_SLEEP_PIN      (1UL << 9U)
#define PED_REG_TRIGGER_PIN    (1UL << 13U)
#define PED_REG_PSON_PIN       (1UL << 14U)

#define PED_REG_POWER_DELAY_MS (50UL)
#define PED_REG_EVENT_SIZE     (8U)

static volatile uint8_t ped_reg_trigger_pending;
static volatile uint8_t ped_reg_power_alarm_pending;
static volatile uint8_t ped_reg_ready_alarm_pending;

static void ped_reg_delay_cycles(uint32_t cycles) {
    while (cycles > 0U) {
        __asm volatile ("nop");
        --cycles;
    }
}

static void ped_reg_delay_ms(uint32_t ms) {
    uint32_t count;

    while (ms > 0U) {
        count = 8000UL;
        ped_reg_delay_cycles(count);
        --ms;
    }
}

static void ped_reg_write_le_u16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (uint8_t)(value & 0x00FFU);
    buffer[1] = (uint8_t)((value >> 8U) & 0x00FFU);
}

static uint16_t ped_reg_read_le_u16(const uint8_t* buffer) {
    return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U);
}

static void ped_reg_set_pin_output(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER &= ~(3UL << (pin_index * 2UL));
    gpio->MODER |= 1UL << (pin_index * 2UL);
}

static void ped_reg_set_pin_input(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER &= ~(3UL << (pin_index * 2UL));
}

static void ped_reg_set_pin_analog(GPIO_TypeDef* gpio, uint32_t pin_index) {
    gpio->MODER |= 3UL << (pin_index * 2UL);
}

static void ped_reg_configure_bus_safe(void) {
    GPIOD->MODER = 0xFFFFFFFFUL;
    GPIOF->MODER = 0xFFFFFFFFUL;

    ped_reg_set_pin_analog(GPIOE, 7UL);
    ped_reg_set_pin_analog(GPIOE, 8UL);
    ped_reg_set_pin_analog(GPIOE, 9UL);
}

static void ped_reg_configure_power_control(void) {
    ped_reg_set_pin_output(GPIOE, 6UL);
    GPIOE->BSRR = PED_REG_POWER_PIN << 16U;
}

static void ped_reg_configure_monitor_inputs(void) {
    ped_reg_set_pin_input(GPIOG, 0UL);
    ped_reg_set_pin_input(GPIOG, 13UL);
    ped_reg_set_pin_input(GPIOG, 14UL);

    GPIOG->PUPDR &= ~(3UL << (0UL * 2UL));
    GPIOG->PUPDR &= ~(3UL << (13UL * 2UL));
    GPIOG->PUPDR &= ~(3UL << (14UL * 2UL));
}

static void ped_reg_configure_exti(void) {
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    SYSCFG->EXTICR[3] &= ~(SYSCFG_EXTICR4_EXTI13 | SYSCFG_EXTICR4_EXTI14);
    SYSCFG->EXTICR[3] |= 6UL << SYSCFG_EXTICR4_EXTI13_Pos;
    SYSCFG->EXTICR[3] |= 6UL << SYSCFG_EXTICR4_EXTI14_Pos;

    EXTI->FTSR1 |= EXTI_FTSR1_FT14;
    EXTI->RTSR1 |= EXTI_RTSR1_RT13;

    EXTI->PR1 = EXTI_PR1_PIF13 | EXTI_PR1_PIF14;
    EXTI->IMR1 |= EXTI_IMR1_IM13 | EXTI_IMR1_IM14;

    NVIC_SetPriority(EXTI15_10_IRQn, 1U);
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

static void ped_reg_configure_active_bus(void) {
    GPIOF->MODER &= ~(GPIO_MODER_MODE8 | GPIO_MODER_MODE9);
    GPIOF->MODER |= (1UL << GPIO_MODER_MODE8_Pos) |
        (1UL << GPIO_MODER_MODE9_Pos);

    GPIOF->BSRR = (PED_REG_DIR_PIN | PED_REG_WRCLK_PIN) << 16U;

    GPIOF->MODER &= 0xFFFF0000UL;
    GPIOF->MODER |= 0x00005555UL;

    GPIOD->MODER = 0x00000000UL;

    ped_reg_set_pin_output(GPIOE, 7UL);
    ped_reg_set_pin_output(GPIOE, 8UL);
    ped_reg_set_pin_output(GPIOE, 9UL);

    GPIOE->BSRR = PED_REG_TGRES_PIN << 16U;
    GPIOE->BSRR = PED_REG_INHIBIT_PIN << 16U;
    GPIOE->BSRR = PED_REG_SLEEP_PIN << 16U;
}

static uint16_t ped_reg_register_read(uint8_t address) {
    GPIOF->BSRR = (((uint32_t)(~address) & PED_REG_ADDR_MASK) << 16U) |
        ((uint32_t)address & PED_REG_ADDR_MASK);

    __asm volatile ("nop");

    return (uint16_t)(GPIOD->IDR & 0xFFFFUL);
}

static void ped_reg_register_write(uint8_t address, uint16_t data) {
    GPIOF->BSRR = PED_REG_DIR_PIN;

    __asm volatile ("nop");

    GPIOD->MODER = 0x55555555UL;

    GPIOF->BSRR = (((uint32_t)(~address) & PED_REG_ADDR_MASK) << 16U) |
        ((uint32_t)address & PED_REG_ADDR_MASK);

    GPIOD->ODR = data;

    __asm volatile ("nop");

    GPIOF->BSRR = PED_REG_WRCLK_PIN;

    __asm volatile ("nop");

    GPIOF->BSRR = PED_REG_WRCLK_PIN << 16U;
    GPIOF->BSRR = PED_REG_DIR_PIN << 16U;

    GPIOD->MODER = 0x00000000UL;
}

static BoardStatus ped_reg_check_ready(void) {
    if ((GPIOG->IDR & PED_REG_READY_PIN) != 0UL) {
        ped_reg_ready_alarm_pending = 1U;
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}

static BoardStatus ped_reg_check_power_good(void) {
    if ((GPIOG->IDR & PED_REG_PSON_PIN) == 0UL) {
        ped_reg_power_alarm_pending = 1U;
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}

BoardStatus ped_reg_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIODEN |
        RCC_AHB2ENR_GPIOEEN |
        RCC_AHB2ENR_GPIOFEN |
        RCC_AHB2ENR_GPIOGEN;

    ped_reg_trigger_pending = 0U;
    ped_reg_power_alarm_pending = 0U;
    ped_reg_ready_alarm_pending = 0U;

    ped_reg_configure_bus_safe();
    ped_reg_configure_power_control();
    ped_reg_configure_monitor_inputs();
    ped_reg_configure_exti();

    return BOARD_OK;
}

BoardStatus ped_reg_power_on(void) {
    BoardStatus status;

    GPIOE->BSRR = PED_REG_POWER_PIN;

    ped_reg_delay_ms(PED_REG_POWER_DELAY_MS);

    status = ped_reg_check_power_good();
    if (status != BOARD_OK) {
        (void)ped_reg_power_off();
        return status;
    }

    status = ped_reg_check_ready();
    if (status != BOARD_OK) {
        (void)ped_reg_power_off();
        return status;
    }

    ped_reg_configure_active_bus();

    return BOARD_OK;
}

BoardStatus ped_reg_power_off(void) {
    GPIOE->BSRR = PED_REG_POWER_PIN << 16U;
    ped_reg_configure_bus_safe();

    return BOARD_OK;
}

BoardStatus ped_reg_is_powered(uint8_t* is_powered) {
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = ((GPIOG->IDR & PED_REG_PSON_PIN) != 0UL) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus ped_reg_read_status(uint32_t* status) {
    BoardStatus board_status;

    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_status = ped_reg_check_power_good();
    if (board_status != BOARD_OK) {
        *status = 0U;
        return board_status;
    }

    board_status = ped_reg_check_ready();
    if (board_status != BOARD_OK) {
        *status = 0U;
        return board_status;
    }

    *status = (uint32_t)ped_reg_register_read(0x03U);

    return BOARD_OK;
}

BoardStatus ped_reg_write_config(const void* config, size_t size) {
    const uint8_t* bytes;
    size_t index;
    size_t word_count;
    uint16_t value;
    BoardStatus status;

    if ((config == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((size % 2U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((size / 2U) > 256U) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ped_reg_check_power_good();
    if (status != BOARD_OK) {
        return status;
    }

    status = ped_reg_check_ready();
    if (status != BOARD_OK) {
        return status;
    }

    bytes = config;
    word_count = size / 2U;

    for (index = 0U; index < word_count; ++index) {
        value = ped_reg_read_le_u16(&bytes[index * 2U]);
        ped_reg_register_write((uint8_t)index, value);
    }

    return BOARD_OK;
}

BoardStatus ped_reg_read_event(void* event_buffer,
                               size_t buffer_size,
                               size_t* bytes_read) {
    uint8_t* buffer;
    PedRegEvent event;
    BoardStatus status;

    if (bytes_read == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_read = 0U;

    if ((event_buffer == 0) || (buffer_size < PED_REG_EVENT_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ped_reg_check_power_good();
    if (status != BOARD_OK) {
        return status;
    }

    status = ped_reg_check_ready();
    if (status != BOARD_OK) {
        return status;
    }

    buffer = event_buffer;

    event.amp = ped_reg_register_read(0x02U);
    event.status = ped_reg_register_read(0x03U);
    event.trigger = ped_reg_register_read(0x04U);
    event.dead_time = ped_reg_register_read(0x05U);

    ped_reg_write_le_u16(&buffer[0], event.amp);
    ped_reg_write_le_u16(&buffer[2], event.status);
    ped_reg_write_le_u16(&buffer[4], event.trigger);
    ped_reg_write_le_u16(&buffer[6], event.dead_time);

    (void)ped_reg_reset_trigger();

    *bytes_read = PED_REG_EVENT_SIZE;

    return BOARD_OK;
}

BoardStatus ped_reg_set_inhibit(uint8_t enabled) {
    if (enabled != 0U) {
        GPIOE->BSRR = PED_REG_INHIBIT_PIN;
    } else {
        GPIOE->BSRR = PED_REG_INHIBIT_PIN << 16U;
    }

    return BOARD_OK;
}

BoardStatus ped_reg_set_sleep(uint8_t enabled) {
    if (enabled != 0U) {
        GPIOE->BSRR = PED_REG_SLEEP_PIN;
    } else {
        GPIOE->BSRR = PED_REG_SLEEP_PIN << 16U;
    }

    return BOARD_OK;
}

BoardStatus ped_reg_reset_trigger(void) {
    GPIOE->BSRR = PED_REG_TGRES_PIN;

    __asm volatile ("nop");
    __asm volatile ("nop");

    GPIOE->BSRR = PED_REG_TGRES_PIN << 16U;

    return BOARD_OK;
}

void ped_reg_handle_exti15_10_irq(void) {
    if ((EXTI->PR1 & EXTI_PR1_PIF14) != 0UL) {
        GPIOE->BSRR = PED_REG_POWER_PIN << 16U;
        ped_reg_configure_bus_safe();
        ped_reg_power_alarm_pending = 1U;
        EXTI->PR1 = EXTI_PR1_PIF14;
    }

    if ((EXTI->PR1 & EXTI_PR1_PIF13) != 0UL) {
        ped_reg_trigger_pending = 1U;
        EXTI->PR1 = EXTI_PR1_PIF13;
    }
}

BoardStatus ped_reg_take_trigger_pending(uint8_t* pending) {
    uint32_t primask;

    if (pending == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    *pending = ped_reg_trigger_pending;
    ped_reg_trigger_pending = 0U;

    if ((primask & 1UL) == 0UL) {
        __enable_irq();
    }

    return BOARD_OK;
}

BoardStatus ped_reg_take_power_alarm_pending(uint8_t* pending) {
    uint32_t primask;

    if (pending == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    *pending = ped_reg_power_alarm_pending;
    ped_reg_power_alarm_pending = 0U;

    if ((primask & 1UL) == 0UL) {
        __enable_irq();
    }

    return BOARD_OK;
}

BoardStatus ped_reg_take_ready_alarm_pending(uint8_t* pending) {
    uint32_t primask;

    if (pending == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    *pending = ped_reg_ready_alarm_pending;
    ped_reg_ready_alarm_pending = 0U;

    if ((primask & 1UL) == 0UL) {
        __enable_irq();
    }

    return BOARD_OK;
}
