#include "i2c.h"

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "gpio.h"
#include "stm32l496xx.h"

#define I2C_TIMEOUT (1000000UL)
#define I2C_RECOVERY_DELAY_CYCLES (2000UL)
#define I2C_MAX_TRANSFER_SIZE (255UL)

#ifndef NATALIA_I2C_TIMINGR_100KHZ
#define NATALIA_I2C_TIMINGR_100KHZ (0x10909CECUL)
#endif

#ifndef NATALIA_I2C_TIMINGR_400KHZ
#define NATALIA_I2C_TIMINGR_400KHZ (0x00702991UL)
#endif

typedef struct {
    I2C_TypeDef* instance;
    BoardPinId scl_pin;
    BoardPinId sda_pin;
    uint32_t enable_mask;
    uint32_t reset_mask;
    I2cSpeed speed;
    uint8_t initialized;
} I2cBusState;

static I2cBusState i2c_bus_states[] = {
    [I2C_BUS_POWER] = {
        .instance = I2C1,
        .scl_pin = BOARD_PIN_I2C1_SCL,
        .sda_pin = BOARD_PIN_I2C1_SDA,
        .enable_mask = RCC_APB1ENR1_I2C1EN,
        .reset_mask = RCC_APB1RSTR1_I2C1RST,
        .speed = I2C_SPEED_100KHZ,
        .initialized = 0U
    },
    [I2C_BUS_TEMP] = {
        .instance = I2C2,
        .scl_pin = BOARD_PIN_I2C2_SCL,
        .sda_pin = BOARD_PIN_I2C2_SDA,
        .enable_mask = RCC_APB1ENR1_I2C2EN,
        .reset_mask = RCC_APB1RSTR1_I2C2RST,
        .speed = I2C_SPEED_100KHZ,
        .initialized = 0U
    },
    [I2C_BUS_PED] = {
        .instance = I2C3,
        .scl_pin = BOARD_PIN_I2C3_SCL,
        .sda_pin = BOARD_PIN_I2C3_SDA,
        .enable_mask = RCC_APB1ENR1_I2C3EN,
        .reset_mask = RCC_APB1RSTR1_I2C3RST,
        .speed = I2C_SPEED_100KHZ,
        .initialized = 0U
    }
};

static void i2c_delay_cycles(uint32_t cycles) {
    while (cycles > 0U) {
        __NOP();
        --cycles;
    }
}

static BoardStatus i2c_get_bus_state(I2cBusId bus, I2cBusState** state) {
    if (state == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((uint32_t)bus >= (uint32_t)(sizeof(i2c_bus_states) / sizeof(i2c_bus_states[0]))) {
        return BOARD_ERR_INVALID_ARG;
    }

    *state = &i2c_bus_states[(uint32_t)bus];

    return BOARD_OK;
}

static uint32_t i2c_get_timing(I2cSpeed speed) {
    if (speed == I2C_SPEED_400KHZ) {
        return NATALIA_I2C_TIMINGR_400KHZ;
    }

    return NATALIA_I2C_TIMINGR_100KHZ;
}

static BoardStatus i2c_validate_address(uint8_t address_7bit) {
    if (address_7bit < 0x08U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (address_7bit > 0x77U) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus i2c_validate_size(size_t size) {
    if (size == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > I2C_MAX_TRANSFER_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus i2c_configure_pins(const I2cBusState* state) {
    GpioConfig config;
    BoardStatus status;

    if (state == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    config.mode = GPIO_MODE_ALTERNATE;
    config.pull = GPIO_PULL_UP;
    config.output_type = GPIO_OUTPUT_OPEN_DRAIN;
    config.speed = GPIO_SPEED_HIGH;
    config.initial_level = GPIO_LEVEL_HIGH;

    status = gpio_configure(state->scl_pin, &config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(state->sda_pin, &config);
}

static void i2c_enable_clock(const I2cBusState* state) {
    RCC->APB1ENR1 |= state->enable_mask;
    (void)RCC->APB1ENR1;
}

static void i2c_disable_clock(const I2cBusState* state) {
    RCC->APB1ENR1 &= ~state->enable_mask;
    (void)RCC->APB1ENR1;
}

static void i2c_reset_peripheral(const I2cBusState* state) {
    RCC->APB1RSTR1 |= state->reset_mask;
    (void)RCC->APB1RSTR1;

    RCC->APB1RSTR1 &= ~state->reset_mask;
    (void)RCC->APB1RSTR1;
}

static void i2c_clear_flags(I2C_TypeDef* instance) {
    instance->ICR = I2C_ICR_NACKCF |
        I2C_ICR_STOPCF |
        I2C_ICR_BERRCF |
        I2C_ICR_ARLOCF |
        I2C_ICR_OVRCF |
        I2C_ICR_TIMOUTCF |
        I2C_ICR_PECCF |
        I2C_ICR_ALERTCF;
}

static BoardStatus i2c_abort_transfer(I2C_TypeDef* instance) {
    uint32_t timeout;

    if (instance == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((instance->ISR & I2C_ISR_STOPF) == 0U) {
        if ((instance->ISR & I2C_ISR_BUSY) != 0U) {
            instance->CR2 |= I2C_CR2_STOP;
        }
    }

    timeout = I2C_TIMEOUT;

    while ((instance->ISR & I2C_ISR_BUSY) != 0U) {
        if ((instance->ISR & I2C_ISR_STOPF) != 0U) {
            break;
        }

        if (timeout == 0U) {
            i2c_clear_flags(instance);
            instance->CR2 = 0U;
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    i2c_clear_flags(instance);
    instance->CR2 = 0U;

    return BOARD_OK;
}

static BoardStatus i2c_check_transfer_error(I2C_TypeDef* instance) {
    uint32_t isr;

    isr = instance->ISR;

    if ((isr & I2C_ISR_NACKF) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_NOT_READY;
    }

    if ((isr & I2C_ISR_BERR) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_IO;
    }

    if ((isr & I2C_ISR_ARLO) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_BUSY;
    }

    if ((isr & I2C_ISR_OVR) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_IO;
    }

    if ((isr & I2C_ISR_TIMEOUT) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_TIMEOUT;
    }

    if ((isr & I2C_ISR_PECERR) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_IO;
    }

    if ((isr & I2C_ISR_ALERT) != 0U) {
        (void)i2c_abort_transfer(instance);
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus i2c_wait_bus_idle(I2C_TypeDef* instance) {
    uint32_t timeout;

    timeout = I2C_TIMEOUT;

    while ((instance->ISR & I2C_ISR_BUSY) != 0U) {
        if (timeout == 0U) {
            (void)i2c_abort_transfer(instance);
            return BOARD_ERR_BUSY;
        }

        --timeout;
    }

    instance->CR2 = 0U;

    return BOARD_OK;
}

static BoardStatus i2c_wait_flag(I2C_TypeDef* instance, uint32_t flag) {
    uint32_t timeout;
    BoardStatus status;

    timeout = I2C_TIMEOUT;

    while ((instance->ISR & flag) == 0U) {
        status = i2c_check_transfer_error(instance);
        if (status != BOARD_OK) {
            return status;
        }

        if (timeout == 0U) {
            (void)i2c_abort_transfer(instance);
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus i2c_wait_stop(I2C_TypeDef* instance) {
    uint32_t timeout;
    BoardStatus status;

    timeout = I2C_TIMEOUT;

    while ((instance->ISR & I2C_ISR_STOPF) == 0U) {
        status = i2c_check_transfer_error(instance);
        if (status != BOARD_OK) {
            return status;
        }

        if (timeout == 0U) {
            (void)i2c_abort_transfer(instance);
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    instance->ICR = I2C_ICR_STOPCF;
    instance->CR2 = 0U;

    return BOARD_OK;
}

static BoardStatus i2c_start_transfer(I2C_TypeDef* instance,
                                      uint8_t address_7bit,
                                      size_t size,
                                      uint8_t read,
                                      uint8_t autoend) {
    uint32_t cr2;

    if (size > I2C_MAX_TRANSFER_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    cr2 = ((uint32_t)address_7bit << 1U) |
        ((uint32_t)size << I2C_CR2_NBYTES_Pos);

    if (read != 0U) {
        cr2 |= I2C_CR2_RD_WRN;
    }

    if (autoend != 0U) {
        cr2 |= I2C_CR2_AUTOEND;
    }

    cr2 |= I2C_CR2_START;

    instance->CR2 = cr2;

    return BOARD_OK;
}

BoardStatus i2c_init_bus(I2cBusId bus) {
    return i2c_init_bus_speed(bus, I2C_SPEED_100KHZ);
}

BoardStatus i2c_init_bus_speed(I2cBusId bus, I2cSpeed speed) {
    I2cBusState* state;
    I2C_TypeDef* instance;
    BoardStatus status;

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    if ((speed != I2C_SPEED_100KHZ) && (speed != I2C_SPEED_400KHZ)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = i2c_configure_pins(state);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_enable_clock(state);
    i2c_reset_peripheral(state);

    instance = state->instance;

    instance->CR1 &= ~I2C_CR1_PE;
    instance->CR2 = 0U;
    i2c_clear_flags(instance);

    instance->TIMINGR = i2c_get_timing(speed);
    instance->CR1 = 0U;
    instance->CR1 |= I2C_CR1_PE;

    state->speed = speed;
    state->initialized = 1U;

    return BOARD_OK;
}

BoardStatus i2c_deinit_bus(I2cBusId bus) {
    I2cBusState* state;
    BoardStatus status;

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_enable_clock(state);

    state->instance->CR1 &= ~I2C_CR1_PE;
    state->instance->CR2 = 0U;
    i2c_clear_flags(state->instance);

    (void)gpio_set_disconnected(state->scl_pin);
    (void)gpio_set_disconnected(state->sda_pin);

    i2c_disable_clock(state);

    state->initialized = 0U;

    return BOARD_OK;
}

BoardStatus i2c_write(I2cBusId bus, uint8_t address_7bit, const void* data, size_t size) {
    I2cBusState* state;
    I2C_TypeDef* instance;
    const uint8_t* bytes;
    size_t index;
    BoardStatus status;

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_address(address_7bit);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_size(size);
    if (status != BOARD_OK) {
        return status;
    }

    if (data == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    instance = state->instance;
    bytes = (const uint8_t*)data;

    status = i2c_wait_bus_idle(instance);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_clear_flags(instance);

    status = i2c_start_transfer(instance, address_7bit, size, 0U, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < size; ++index) {
        status = i2c_wait_flag(instance, I2C_ISR_TXIS);
        if (status != BOARD_OK) {
            return status;
        }

        instance->TXDR = bytes[index];
    }

    return i2c_wait_stop(instance);
}

BoardStatus i2c_read(I2cBusId bus, uint8_t address_7bit, void* data, size_t size) {
    I2cBusState* state;
    I2C_TypeDef* instance;
    uint8_t* bytes;
    size_t index;
    BoardStatus status;

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_address(address_7bit);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_size(size);
    if (status != BOARD_OK) {
        return status;
    }

    if (data == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    instance = state->instance;
    bytes = (uint8_t*)data;

    status = i2c_wait_bus_idle(instance);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_clear_flags(instance);

    status = i2c_start_transfer(instance, address_7bit, size, 1U, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < size; ++index) {
        status = i2c_wait_flag(instance, I2C_ISR_RXNE);
        if (status != BOARD_OK) {
            return status;
        }

        bytes[index] = (uint8_t)instance->RXDR;
    }

    return i2c_wait_stop(instance);
}

BoardStatus i2c_write_read(I2cBusId bus,
                           uint8_t address_7bit,
                           const void* tx_data,
                           size_t tx_size,
                           void* rx_data,
                           size_t rx_size) {
    I2cBusState* state;
    I2C_TypeDef* instance;
    const uint8_t* tx_bytes;
    uint8_t* rx_bytes;
    size_t index;
    BoardStatus status;

    if (tx_size == 0U) {
        return i2c_read(bus, address_7bit, rx_data, rx_size);
    }

    if (rx_size == 0U) {
        return i2c_write(bus, address_7bit, tx_data, tx_size);
    }

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_address(address_7bit);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_size(tx_size);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_validate_size(rx_size);
    if (status != BOARD_OK) {
        return status;
    }

    if ((tx_data == 0) || (rx_data == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    instance = state->instance;
    tx_bytes = (const uint8_t*)tx_data;
    rx_bytes = (uint8_t*)rx_data;

    status = i2c_wait_bus_idle(instance);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_clear_flags(instance);

    status = i2c_start_transfer(instance, address_7bit, tx_size, 0U, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < tx_size; ++index) {
        status = i2c_wait_flag(instance, I2C_ISR_TXIS);
        if (status != BOARD_OK) {
            return status;
        }

        instance->TXDR = tx_bytes[index];
    }

    status = i2c_wait_stop(instance);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_start_transfer(instance, address_7bit, rx_size, 1U, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < rx_size; ++index) {
        status = i2c_wait_flag(instance, I2C_ISR_RXNE);
        if (status != BOARD_OK) {
            return status;
        }

        rx_bytes[index] = (uint8_t)instance->RXDR;
    }

    return i2c_wait_stop(instance);
}

BoardStatus i2c_probe(I2cBusId bus, uint8_t address_7bit, uint8_t* is_present) {
    uint8_t value;
    BoardStatus status;

    if (is_present == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_present = 0U;

    status = i2c_read(bus, address_7bit, &value, 1U);
    if (status == BOARD_OK) {
        *is_present = 1U;
        return BOARD_OK;
    }

    if (status == BOARD_ERR_NOT_READY) {
        return BOARD_OK;
    }

    return status;
}

BoardStatus i2c_recover_bus(I2cBusId bus) {
    I2cBusState* state;
    GpioConfig scl_config;
    GpioConfig sda_config;
    GpioLevel sda_level;
    uint32_t index;
    BoardStatus status;

    status = i2c_get_bus_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    i2c_enable_clock(state);
    state->instance->CR1 &= ~I2C_CR1_PE;
    state->instance->CR2 = 0U;
    i2c_clear_flags(state->instance);

    scl_config.mode = GPIO_MODE_OUTPUT;
    scl_config.pull = GPIO_PULL_NONE;
    scl_config.output_type = GPIO_OUTPUT_OPEN_DRAIN;
    scl_config.speed = GPIO_SPEED_LOW;
    scl_config.initial_level = GPIO_LEVEL_HIGH;

    sda_config.mode = GPIO_MODE_OUTPUT;
    sda_config.pull = GPIO_PULL_NONE;
    sda_config.output_type = GPIO_OUTPUT_OPEN_DRAIN;
    sda_config.speed = GPIO_SPEED_LOW;
    sda_config.initial_level = GPIO_LEVEL_HIGH;

    status = gpio_configure(state->scl_pin, &scl_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(state->sda_pin, &sda_config);
    if (status != BOARD_OK) {
        return status;
    }

    (void)gpio_write(state->sda_pin, GPIO_LEVEL_HIGH);
    i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);

    for (index = 0U; index < 9U; ++index) {
        (void)gpio_write(state->scl_pin, GPIO_LEVEL_LOW);
        i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);

        (void)gpio_write(state->scl_pin, GPIO_LEVEL_HIGH);
        i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);
    }

    (void)gpio_write(state->sda_pin, GPIO_LEVEL_LOW);
    i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);

    (void)gpio_write(state->scl_pin, GPIO_LEVEL_HIGH);
    i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);

    (void)gpio_write(state->sda_pin, GPIO_LEVEL_HIGH);
    i2c_delay_cycles(I2C_RECOVERY_DELAY_CYCLES);

    status = gpio_read(state->sda_pin, &sda_level);
    if (status != BOARD_OK) {
        return status;
    }

    if (sda_level != GPIO_LEVEL_HIGH) {
        return BOARD_ERR_BUSY;
    }

    return i2c_init_bus_speed(bus, state->speed);
}
