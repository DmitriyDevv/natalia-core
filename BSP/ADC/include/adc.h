#ifndef NATALIA_ADC_H
#define NATALIA_ADC_H

#include <stdint.h>

#include "status.h"

typedef enum {
    ADC_INPUT_TERM_A = 0
} AdcInputId;

typedef struct {
    uint16_t raw;
    uint16_t vrefint_raw;
    uint32_t vdda_mv;
    uint32_t millivolts;
    uint32_t sequence;
    uint8_t ready;
} AdcSample;

BoardStatus adc_init(void);
BoardStatus adc_start_sample(void);
BoardStatus adc_stop(void);
BoardStatus adc_is_busy(uint8_t* is_busy);
BoardStatus adc_has_ready_sample(uint8_t* has_sample);
BoardStatus adc_read_input(AdcInputId input, AdcSample* sample);

#endif
