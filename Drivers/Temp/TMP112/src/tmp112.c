#include "tmp112.h"

#include <stdint.h>

#define TMP112_REG_TEMPERATURE (0x00U)
#define TMP112_REG_CONFIGURATION (0x01U)

#define TMP112_CONFIG_MSB_OS (0x80U)
#define TMP112_CONFIG_MSB_SD (0x01U)
#define TMP112_CONFIG_LSB_EM (0x10U)

#define TMP112_MIN_TEMPERATURE_MILLI_C (-40000L)
#define TMP112_MAX_TEMPERATURE_MILLI_C (125000L)

static uint8_t tmp112_initialized;

static BoardStatus tmp112_validate_address(uint8_t address_7bit) {
    if (address_7bit < TMP112_ADDRESS_MIN) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (address_7bit > TMP112_ADDRESS_MAX) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static int16_t tmp112_raw_word_to_raw_12bit(uint16_t raw_word) {
    int16_t raw_12bit;

    raw_12bit = (int16_t)(raw_word >> 4U);

    if ((raw_12bit & 0x0800) != 0) {
        raw_12bit = (int16_t)(raw_12bit | (int16_t)0xF000);
    }

    return raw_12bit;
}

static int32_t tmp112_raw_12bit_to_milli_c(int16_t raw_12bit) {
    int32_t scaled;

    scaled = (int32_t)raw_12bit * 625L;

    if (scaled >= 0) {
        return (scaled + 5L) / 10L;
    }

    return (scaled - 5L) / 10L;
}

static uint8_t tmp112_is_range_valid(int32_t temperature_milli_c) {
    if (temperature_milli_c < TMP112_MIN_TEMPERATURE_MILLI_C) {
        return 0U;
    }

    if (temperature_milli_c > TMP112_MAX_TEMPERATURE_MILLI_C) {
        return 0U;
    }

    return 1U;
}

static BoardStatus tmp112_read_register_16(uint8_t address_7bit, uint8_t reg, uint8_t* msb, uint8_t* lsb) {
    uint8_t rx[2];
    BoardStatus status;

    if ((msb == 0) || (lsb == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    rx[0] = 0U;
    rx[1] = 0U;

    status = tmp112_validate_address(address_7bit);
    if (status != BOARD_OK) {
        return status;
    }

    if (tmp112_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = i2c_write_read(I2C_BUS_TEMP, address_7bit, &reg, 1U, rx, sizeof(rx));
    if (status != BOARD_OK) {
        return status;
    }

    *msb = rx[0];
    *lsb = rx[1];

    return BOARD_OK;
}

static BoardStatus tmp112_write_register_16(uint8_t address_7bit, uint8_t reg, uint8_t msb, uint8_t lsb) {
    uint8_t tx[3];
    BoardStatus status;

    status = tmp112_validate_address(address_7bit);
    if (status != BOARD_OK) {
        return status;
    }

    if (tmp112_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    tx[0] = reg;
    tx[1] = msb;
    tx[2] = lsb;

    return i2c_write(I2C_BUS_TEMP, address_7bit, tx, sizeof(tx));
}

BoardStatus tmp112_init(I2cSpeed speed) {
    BoardStatus status;

    tmp112_initialized = 0U;

    status = i2c_init_bus_speed(I2C_BUS_TEMP, speed);
    if (status != BOARD_OK) {
        return status;
    }

    tmp112_initialized = 1U;

    return BOARD_OK;
}

BoardStatus tmp112_deinit(void) {
    tmp112_initialized = 0U;

    return i2c_deinit_bus(I2C_BUS_TEMP);
}

BoardStatus tmp112_probe(uint8_t address_7bit, uint8_t* is_present) {
    uint8_t msb;
    uint8_t lsb;
    BoardStatus status;

    if (is_present == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_present = 0U;

    status = tmp112_read_register_16(address_7bit, TMP112_REG_CONFIGURATION, &msb, &lsb);
    if (status == BOARD_OK) {
        *is_present = 1U;
        return BOARD_OK;
    }

    if (status == BOARD_ERR_NOT_READY) {
        return BOARD_OK;
    }

    return status;
}

BoardStatus tmp112_read_register_raw(uint8_t address_7bit, uint8_t reg, uint8_t* msb, uint8_t* lsb) {
    return tmp112_read_register_16(address_7bit, reg, msb, lsb);
}

BoardStatus tmp112_write_register_raw(uint8_t address_7bit, uint8_t reg, uint8_t msb, uint8_t lsb) {
    return tmp112_write_register_16(address_7bit, reg, msb, lsb);
}

BoardStatus tmp112_discover(uint8_t* addresses, size_t address_capacity, size_t* address_count) {
    uint8_t address;
    uint8_t is_present;
    size_t count;
    BoardStatus status;

    if ((addresses == 0) || (address_count == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    count = 0U;

    for (address = TMP112_ADDRESS_MIN; address <= TMP112_ADDRESS_MAX; ++address) {
        status = tmp112_probe(address, &is_present);
        if (status != BOARD_OK) {
            *address_count = count;
            return status;
        }

        if (is_present != 0U) {
            if (count >= address_capacity) {
                *address_count = count;
                return BOARD_ERR_BUSY;
            }

            addresses[count] = address;
            ++count;
        }

        if (address == TMP112_ADDRESS_MAX) {
            break;
        }
    }

    *address_count = count;

    return BOARD_OK;
}

BoardStatus tmp112_read_config(uint8_t address_7bit, Tmp112Config* config) {
    uint8_t msb;
    uint8_t lsb;
    BoardStatus status;

    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = tmp112_read_register_16(address_7bit, TMP112_REG_CONFIGURATION, &msb, &lsb);
    if (status != BOARD_OK) {
        return status;
    }

    config->address_7bit = address_7bit;
    config->config_msb = msb;
    config->config_lsb = lsb;

    if ((msb & TMP112_CONFIG_MSB_SD) != 0U) {
        config->shutdown = 1U;
    } else {
        config->shutdown = 0U;
    }

    if ((msb & TMP112_CONFIG_MSB_OS) != 0U) {
        config->one_shot_ready = 1U;
    } else {
        config->one_shot_ready = 0U;
    }

    if ((lsb & TMP112_CONFIG_LSB_EM) != 0U) {
        config->extended_mode = 1U;
    } else {
        config->extended_mode = 0U;
    }

    return BOARD_OK;
}

BoardStatus tmp112_enter_shutdown(uint8_t address_7bit) {
    Tmp112Config config;
    uint8_t msb;
    uint8_t lsb;
    BoardStatus status;

    status = tmp112_read_config(address_7bit, &config);
    if (status != BOARD_OK) {
        return status;
    }

    msb = config.config_msb | TMP112_CONFIG_MSB_SD;
    lsb = config.config_lsb & (uint8_t)(~TMP112_CONFIG_LSB_EM);

    return tmp112_write_register_16(address_7bit, TMP112_REG_CONFIGURATION, msb, lsb);
}

BoardStatus tmp112_resume_continuous(uint8_t address_7bit) {
    Tmp112Config config;
    uint8_t msb;
    uint8_t lsb;
    BoardStatus status;

    status = tmp112_read_config(address_7bit, &config);
    if (status != BOARD_OK) {
        return status;
    }

    msb = config.config_msb & (uint8_t)(~(TMP112_CONFIG_MSB_SD | TMP112_CONFIG_MSB_OS));
    lsb = config.config_lsb;

    return tmp112_write_register_16(address_7bit, TMP112_REG_CONFIGURATION, msb, lsb);
}

BoardStatus tmp112_start_one_shot(uint8_t address_7bit) {
    Tmp112Config config;
    uint8_t msb;
    uint8_t lsb;
    BoardStatus status;

    status = tmp112_read_config(address_7bit, &config);
    if (status != BOARD_OK) {
        return status;
    }

    msb = config.config_msb | TMP112_CONFIG_MSB_SD | TMP112_CONFIG_MSB_OS;
    lsb = config.config_lsb & (uint8_t)(~TMP112_CONFIG_LSB_EM);

    return tmp112_write_register_16(address_7bit, TMP112_REG_CONFIGURATION, msb, lsb);
}

BoardStatus tmp112_is_one_shot_ready(uint8_t address_7bit, uint8_t* is_ready) {
    Tmp112Config config;
    BoardStatus status;

    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 0U;

    status = tmp112_read_config(address_7bit, &config);
    if (status != BOARD_OK) {
        return status;
    }

    *is_ready = config.one_shot_ready;

    return BOARD_OK;
}

BoardStatus tmp112_read_sample(uint8_t address_7bit, Tmp112Sample* sample) {
    uint8_t msb;
    uint8_t lsb;
    uint16_t raw_word;
    int16_t raw_12bit;
    int32_t temperature_milli_c;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    sample->address_7bit = address_7bit;
    sample->temperature_milli_c = 0;
    sample->raw_word = 0U;
    sample->raw_12bit = 0;
    sample->msb = 0U;
    sample->lsb = 0U;
    sample->range_valid = 0U;

    status = tmp112_read_register_16(address_7bit, TMP112_REG_TEMPERATURE, &msb, &lsb);
    if (status != BOARD_OK) {
        return status;
    }

    raw_word = ((uint16_t)msb << 8U) | (uint16_t)lsb;
    raw_12bit = tmp112_raw_word_to_raw_12bit(raw_word);
    temperature_milli_c = tmp112_raw_12bit_to_milli_c(raw_12bit);

    sample->temperature_milli_c = temperature_milli_c;
    sample->raw_word = raw_word;
    sample->raw_12bit = raw_12bit;
    sample->msb = msb;
    sample->lsb = lsb;
    sample->range_valid = tmp112_is_range_valid(temperature_milli_c);

    return BOARD_OK;
}

BoardStatus tmp112_read_temperature_milli_c(uint8_t address_7bit, int32_t* temperature_milli_c) {
    Tmp112Sample sample;
    BoardStatus status;

    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = tmp112_read_sample(address_7bit, &sample);
    if (status != BOARD_OK) {
        return status;
    }

    *temperature_milli_c = sample.temperature_milli_c;

    return BOARD_OK;
}
