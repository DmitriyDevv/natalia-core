#ifndef NATALIA_TMP112_CONFIG_H
#define NATALIA_TMP112_CONFIG_H

#include "i2c.h"

/*
 * PU/PED TMP112 digital temperature sensor configuration.
 * Addresses are provisional (0x48-0x4B range) - adjust to the real board wiring.
 */

#ifndef TMP112_CONFIG_SPEED
#define TMP112_CONFIG_SPEED I2C_SPEED_100KHZ
#endif

#ifndef TMP112_CONFIG_PU_ADDRESS_7BIT
#define TMP112_CONFIG_PU_ADDRESS_7BIT 0x48U
#endif

#ifndef TMP112_CONFIG_PED_ADDRESS_7BIT
#define TMP112_CONFIG_PED_ADDRESS_7BIT 0x49U
#endif

#endif /* NATALIA_TMP112_CONFIG_H */
