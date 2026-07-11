#ifndef NATALIA_TC1047_H
#define NATALIA_TC1047_H

#include <stdint.h>

#include "status.h"

typedef struct {
    int32_t temperature_milli_c;
    uint32_t millivolts;
    uint32_t vdda_mv;
    uint32_t adc_sequence;
    uint16_t raw;
    uint16_t vrefint_raw;
    uint8_t ready;
    uint8_t range_valid;
} Tc1047Sample;

BoardStatus tc1047_init(void);
BoardStatus tc1047_start(void);
BoardStatus tc1047_stop(void);
BoardStatus tc1047_read(Tc1047Sample* sample);
BoardStatus tc1047_read_temperature_milli_c(int32_t* temperature_milli_c);

#endif
