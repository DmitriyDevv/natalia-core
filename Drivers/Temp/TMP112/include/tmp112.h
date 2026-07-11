#ifndef NATALIA_TMP112_H
#define NATALIA_TMP112_H

#include <stddef.h>
#include <stdint.h>

#include "i2c.h"
#include "status.h"

#define TMP112_ADDRESS_MIN (0x48U)
#define TMP112_ADDRESS_MAX (0x4BU)

typedef struct {
    uint8_t address_7bit;
    uint8_t config_msb;
    uint8_t config_lsb;
    uint8_t shutdown;
    uint8_t one_shot_ready;
    uint8_t extended_mode;
} Tmp112Config;

typedef struct {
    uint8_t address_7bit;
    int32_t temperature_milli_c;
    uint16_t raw_word;
    int16_t raw_12bit;
    uint8_t msb;
    uint8_t lsb;
    uint8_t range_valid;
} Tmp112Sample;

BoardStatus tmp112_init(I2cSpeed speed);
BoardStatus tmp112_deinit(void);

BoardStatus tmp112_probe(uint8_t address_7bit, uint8_t* is_present);
BoardStatus tmp112_discover(uint8_t* addresses, size_t address_capacity, size_t* address_count);

BoardStatus tmp112_read_config(uint8_t address_7bit, Tmp112Config* config);
BoardStatus tmp112_enter_shutdown(uint8_t address_7bit);
BoardStatus tmp112_resume_continuous(uint8_t address_7bit);
BoardStatus tmp112_start_one_shot(uint8_t address_7bit);
BoardStatus tmp112_is_one_shot_ready(uint8_t address_7bit, uint8_t* is_ready);

BoardStatus tmp112_read_register_raw(uint8_t address_7bit, uint8_t reg, uint8_t* msb, uint8_t* lsb);
BoardStatus tmp112_write_register_raw(uint8_t address_7bit, uint8_t reg, uint8_t msb, uint8_t lsb);

BoardStatus tmp112_read_sample(uint8_t address_7bit, Tmp112Sample* sample);
BoardStatus tmp112_read_temperature_milli_c(uint8_t address_7bit, int32_t* temperature_milli_c);

#endif
