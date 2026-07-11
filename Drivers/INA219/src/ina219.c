#include "ina219.h"

#include <stdint.h>

#define INA219_REGISTER_CONFIG 0x00U
#define INA219_REGISTER_SHUNT_VOLTAGE 0x01U
#define INA219_REGISTER_BUS_VOLTAGE 0x02U
#define INA219_REGISTER_POWER 0x03U
#define INA219_REGISTER_CURRENT 0x04U
#define INA219_REGISTER_CALIBRATION 0x05U

#define INA219_CONFIG_RESET 0x8000U
#define INA219_BUS_VOLTAGE_CNVR_BIT 0x0002U
#define INA219_BUS_VOLTAGE_OVF_BIT 0x0001U

static BoardStatus ina219_validate_config(const Ina219Config* config) {
    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (config->address_7bit < 0x08U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (config->address_7bit > 0x77U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config->speed != I2C_SPEED_100KHZ) &&
        (config->speed != I2C_SPEED_400KHZ)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (config->current_lsb_ua < 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus ina219_require_initialized(Ina219Device* device) {
    if (device == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (device->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}

static BoardStatus ina219_write_register_by_config(const Ina219Config* config,
                                                   uint8_t register_id,
                                                   uint16_t value) {
    uint8_t data[3];

    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    data[0] = register_id;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value & 0x00FFU);

    return i2c_write(config->bus, config->address_7bit, data, sizeof(data));
}

static BoardStatus ina219_read_register_by_config(const Ina219Config* config,
                                                  uint8_t register_id,
                                                  uint16_t* value) {
    uint8_t register_address;
    uint8_t data[2];
    BoardStatus status;

    if ((config == 0) || (value == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    register_address = register_id;

    status = i2c_write_read(config->bus,
                            config->address_7bit,
                            &register_address,
                            sizeof(register_address),
                            data,
                            sizeof(data));
    if (status != BOARD_OK) {
        return status;
    }

    *value = ((uint16_t)data[0] << 8U) | (uint16_t)data[1];

    return BOARD_OK;
}

static int16_t ina219_u16_to_i16(uint16_t value) {
    if (value >= 0x8000U) {
        return (int16_t)((int32_t)value - 65536);
    }

    return (int16_t)value;
}

static BoardStatus ina219_scale_current(const Ina219Device* device,
                                        uint16_t raw_value,
                                        int32_t* microamps) {
    int16_t raw_signed;
    int64_t scaled;

    if ((device == 0) || (microamps == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (device->config.current_lsb_ua <= 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    raw_signed = ina219_u16_to_i16(raw_value);
    scaled = (int64_t)raw_signed * (int64_t)device->config.current_lsb_ua;

    if ((scaled > (int64_t)INT32_MAX) || (scaled < (int64_t)INT32_MIN)) {
        return BOARD_ERR_IO;
    }

    *microamps = (int32_t)scaled;

    return BOARD_OK;
}

BoardStatus ina219_init(Ina219Device* device, const Ina219Config* config) {
    BoardStatus status;

    if (device == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    device->initialized = 0U;

    status = ina219_validate_config(config);
    if (status != BOARD_OK) {
        return status;
    }

    status = i2c_init_bus_speed(config->bus, config->speed);
    if (status != BOARD_OK) {
        return status;
    }

    device->config = *config;

    status = ina219_write_register_by_config(&device->config,
                                             INA219_REGISTER_CONFIG,
                                             device->config.config_register);
    if (status != BOARD_OK) {
        return status;
    }

    if (device->config.calibration_register != 0U) {
        status = ina219_write_register_by_config(&device->config,
                                                 INA219_REGISTER_CALIBRATION,
                                                 device->config.calibration_register);
        if (status != BOARD_OK) {
            return status;
        }
    }

    device->initialized = 1U;

    return BOARD_OK;
}

BoardStatus ina219_reset(Ina219Device* device) {
    BoardStatus status;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    return ina219_write_register_by_config(&device->config,
                                           INA219_REGISTER_CONFIG,
                                           INA219_CONFIG_RESET);
}

BoardStatus ina219_write_config(Ina219Device* device, uint16_t config_register) {
    BoardStatus status;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_write_register_by_config(&device->config,
                                             INA219_REGISTER_CONFIG,
                                             config_register);
    if (status != BOARD_OK) {
        return status;
    }

    device->config.config_register = config_register;

    return BOARD_OK;
}

BoardStatus ina219_write_calibration(Ina219Device* device,
                                     uint16_t calibration_register) {
    BoardStatus status;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_write_register_by_config(&device->config,
                                             INA219_REGISTER_CALIBRATION,
                                             calibration_register);
    if (status != BOARD_OK) {
        return status;
    }

    device->config.calibration_register = calibration_register;

    return BOARD_OK;
}

BoardStatus ina219_read_config(Ina219Device* device, uint16_t* config_register) {
    BoardStatus status;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    return ina219_read_register_by_config(&device->config,
                                          INA219_REGISTER_CONFIG,
                                          config_register);
}

BoardStatus ina219_read_calibration(Ina219Device* device,
                                    uint16_t* calibration_register) {
    BoardStatus status;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    return ina219_read_register_by_config(&device->config,
                                          INA219_REGISTER_CALIBRATION,
                                          calibration_register);
}

BoardStatus ina219_read_bus_voltage(Ina219Device* device,
                                    Ina219BusVoltageSample* sample) {
    uint16_t raw_value;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    sample->millivolts = 0U;
    sample->conversion_ready = 0U;
    sample->math_overflow = 0U;

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_register_by_config(&device->config,
                                            INA219_REGISTER_BUS_VOLTAGE,
                                            &raw_value);
    if (status != BOARD_OK) {
        return status;
    }

    sample->millivolts = ((uint32_t)(raw_value >> 3U)) * 4U;
    sample->conversion_ready = ((raw_value & INA219_BUS_VOLTAGE_CNVR_BIT) != 0U) ? 1U : 0U;
    sample->math_overflow = ((raw_value & INA219_BUS_VOLTAGE_OVF_BIT) != 0U) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus ina219_read_bus_voltage_mv(Ina219Device* device,
                                       uint32_t* millivolts) {
    Ina219BusVoltageSample sample;
    BoardStatus status;

    if (millivolts == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ina219_read_bus_voltage(device, &sample);
    if (status != BOARD_OK) {
        return status;
    }

    *millivolts = sample.millivolts;

    return BOARD_OK;
}

BoardStatus ina219_read_shunt_voltage_uv(Ina219Device* device,
                                         int32_t* microvolts) {
    uint16_t raw_value;
    int16_t raw_signed;
    BoardStatus status;

    if (microvolts == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_register_by_config(&device->config,
                                            INA219_REGISTER_SHUNT_VOLTAGE,
                                            &raw_value);
    if (status != BOARD_OK) {
        return status;
    }

    raw_signed = ina219_u16_to_i16(raw_value);
    *microvolts = (int32_t)raw_signed * 10;

    return BOARD_OK;
}

BoardStatus ina219_read_current_ua(Ina219Device* device,
                                   int32_t* microamps) {
    uint16_t raw_value;
    BoardStatus status;

    if (microamps == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_register_by_config(&device->config,
                                            INA219_REGISTER_CURRENT,
                                            &raw_value);
    if (status != BOARD_OK) {
        return status;
    }

    return ina219_scale_current(device, raw_value, microamps);
}

BoardStatus ina219_read_power_uw(Ina219Device* device,
                                 uint32_t* microwatts) {
    uint16_t raw_value;
    uint64_t scaled;
    BoardStatus status;

    if (microwatts == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ina219_require_initialized(device);
    if (status != BOARD_OK) {
        return status;
    }

    if (device->config.current_lsb_ua <= 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ina219_read_register_by_config(&device->config,
                                            INA219_REGISTER_POWER,
                                            &raw_value);
    if (status != BOARD_OK) {
        return status;
    }

    scaled = (uint64_t)raw_value * 20ULL * (uint64_t)((uint32_t)device->config.current_lsb_ua);

    if (scaled > (uint64_t)UINT32_MAX) {
        return BOARD_ERR_IO;
    }

    *microwatts = (uint32_t)scaled;

    return BOARD_OK;
}

BoardStatus ina219_read_sample(Ina219Device* device, Ina219Sample* sample) {
    Ina219BusVoltageSample bus_sample;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    sample->bus_voltage_mv = 0U;
    sample->shunt_voltage_uv = 0;
    sample->current_ua = 0;
    sample->power_uw = 0U;
    sample->conversion_ready = 0U;
    sample->math_overflow = 0U;

    status = ina219_read_bus_voltage(device, &bus_sample);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_shunt_voltage_uv(device, &sample->shunt_voltage_uv);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_current_ua(device, &sample->current_ua);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_read_power_uw(device, &sample->power_uw);
    if (status != BOARD_OK) {
        return status;
    }

    sample->bus_voltage_mv = bus_sample.millivolts;
    sample->conversion_ready = bus_sample.conversion_ready;
    sample->math_overflow = bus_sample.math_overflow;

    return BOARD_OK;
}
