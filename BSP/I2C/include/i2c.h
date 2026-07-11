#ifndef NATALIA_I2C_H
#define NATALIA_I2C_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    I2C_BUS_POWER = 0,
    I2C_BUS_TEMP,
    I2C_BUS_PED
} I2cBusId;

typedef enum {
    I2C_SPEED_100KHZ = 0,
    I2C_SPEED_400KHZ
} I2cSpeed;

BoardStatus i2c_init_bus(I2cBusId bus);
BoardStatus i2c_init_bus_speed(I2cBusId bus, I2cSpeed speed);
BoardStatus i2c_deinit_bus(I2cBusId bus);

BoardStatus i2c_write(I2cBusId bus, uint8_t address_7bit, const void* data, size_t size);
BoardStatus i2c_read(I2cBusId bus, uint8_t address_7bit, void* data, size_t size);
BoardStatus i2c_write_read(I2cBusId bus,
                           uint8_t address_7bit,
                           const void* tx_data,
                           size_t tx_size,
                           void* rx_data,
                           size_t rx_size);

BoardStatus i2c_probe(I2cBusId bus, uint8_t address_7bit, uint8_t* is_present);
BoardStatus i2c_recover_bus(I2cBusId bus);

#endif
