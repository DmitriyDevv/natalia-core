#include "ina219_config.h"

static const Ina219Config ina219_config_pu = {
    .bus = INA219_CONFIG_PU_BUS,
    .speed = INA219_CONFIG_PU_SPEED,
    .address_7bit = INA219_CONFIG_PU_ADDRESS_7BIT,
    .config_register = INA219_CONFIG_PU_CONFIG_REGISTER,
    .calibration_register = INA219_CONFIG_PU_CALIBRATION_REGISTER,
    .current_lsb_ua = INA219_CONFIG_PU_CURRENT_LSB_UA
};

static const Ina219Config ina219_config_ped = {
    .bus = INA219_CONFIG_PED_BUS,
    .speed = INA219_CONFIG_PED_SPEED,
    .address_7bit = INA219_CONFIG_PED_ADDRESS_7BIT,
    .config_register = INA219_CONFIG_PED_CONFIG_REGISTER,
    .calibration_register = INA219_CONFIG_PED_CALIBRATION_REGISTER,
    .current_lsb_ua = INA219_CONFIG_PED_CURRENT_LSB_UA
};

BoardStatus ina219_config_get(Ina219ConfigChannel channel, const Ina219Config** config) {
    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *config = 0;

    if (channel == INA219_CONFIG_CHANNEL_PU) {
        *config = &ina219_config_pu;
        return BOARD_OK;
    }

    if (channel == INA219_CONFIG_CHANNEL_PED) {
        *config = &ina219_config_ped;
        return BOARD_OK;
    }

    return BOARD_ERR_INVALID_ARG;
}
