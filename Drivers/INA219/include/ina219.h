#ifndef NATALIA_INA219_H
#define NATALIA_INA219_H

#include <stdint.h>

#include "i2c.h"
#include "status.h"

typedef struct {
    I2cBusId bus;
    I2cSpeed speed;
    uint8_t address_7bit;
    uint16_t config_register;
    uint16_t calibration_register;
    int32_t current_lsb_ua;
} Ina219Config;

typedef struct {
    Ina219Config config;
    uint8_t initialized;
} Ina219Device;

typedef struct {
    uint32_t millivolts;
    uint8_t conversion_ready;
    uint8_t math_overflow;
} Ina219BusVoltageSample;

typedef struct {
    uint32_t bus_voltage_mv;
    int32_t shunt_voltage_uv;
    int32_t current_ua;
    uint32_t power_uw;
    uint8_t conversion_ready;
    uint8_t math_overflow;
} Ina219Sample;

BoardStatus ina219_init(Ina219Device* device, const Ina219Config* config);
BoardStatus ina219_reset(Ina219Device* device);

BoardStatus ina219_write_config(Ina219Device* device, uint16_t config_register);
BoardStatus ina219_write_calibration(Ina219Device* device, uint16_t calibration_register);

BoardStatus ina219_read_config(Ina219Device* device, uint16_t* config_register);
BoardStatus ina219_read_calibration(Ina219Device* device, uint16_t* calibration_register);

BoardStatus ina219_read_bus_voltage(Ina219Device* device, Ina219BusVoltageSample* sample);
BoardStatus ina219_read_bus_voltage_mv(Ina219Device* device, uint32_t* millivolts);
BoardStatus ina219_read_shunt_voltage_uv(Ina219Device* device, int32_t* microvolts);
BoardStatus ina219_read_current_ua(Ina219Device* device, int32_t* microamps);
BoardStatus ina219_read_power_uw(Ina219Device* device, uint32_t* microwatts);
BoardStatus ina219_read_sample(Ina219Device* device, Ina219Sample* sample);

#endif
