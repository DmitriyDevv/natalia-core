#include "tc1047.h"

#include <stdint.h>

#include "adc.h"

#define TC1047_MV_AT_0C (500L)
#define TC1047_MV_PER_C (10L)
#define TC1047_MIN_TEMPERATURE_MILLI_C (-40000L)
#define TC1047_MAX_TEMPERATURE_MILLI_C (125000L)

static uint8_t tc1047_initialized;

static int32_t tc1047_convert_mv_to_milli_c(uint32_t millivolts) {
    int64_t delta_mv;
    int64_t temperature;

    delta_mv = (int64_t)millivolts - (int64_t)TC1047_MV_AT_0C;
    temperature = (delta_mv * 1000LL) / (int64_t)TC1047_MV_PER_C;

    if (temperature > INT32_MAX) {
        return INT32_MAX;
    }

    if (temperature < INT32_MIN) {
        return INT32_MIN;
    }

    return (int32_t)temperature;
}

static uint8_t tc1047_is_range_valid(int32_t temperature_milli_c) {
    if (temperature_milli_c < TC1047_MIN_TEMPERATURE_MILLI_C) {
        return 0U;
    }

    if (temperature_milli_c > TC1047_MAX_TEMPERATURE_MILLI_C) {
        return 0U;
    }

    return 1U;
}

BoardStatus tc1047_init(void) {
    BoardStatus status;

    tc1047_initialized = 0U;

    status = adc_init();
    if (status != BOARD_OK) {
        return status;
    }

    tc1047_initialized = 1U;

    return BOARD_OK;
}

BoardStatus tc1047_start(void) {
    if (tc1047_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    return adc_start_sample();
}

BoardStatus tc1047_stop(void) {
    if (tc1047_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    return adc_stop();
}

BoardStatus tc1047_read(Tc1047Sample* sample) {
    AdcSample adc_sample;
    int32_t temperature;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (tc1047_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = adc_read_input(ADC_INPUT_TERM_A, &adc_sample);
    if (status != BOARD_OK) {
        return status;
    }

    temperature = tc1047_convert_mv_to_milli_c(adc_sample.millivolts);

    sample->temperature_milli_c = temperature;
    sample->millivolts = adc_sample.millivolts;
    sample->vdda_mv = adc_sample.vdda_mv;
    sample->adc_sequence = adc_sample.sequence;
    sample->raw = adc_sample.raw;
    sample->vrefint_raw = adc_sample.vrefint_raw;
    sample->ready = adc_sample.ready;
    sample->range_valid = tc1047_is_range_valid(temperature);

    return BOARD_OK;
}

BoardStatus tc1047_read_temperature_milli_c(int32_t* temperature_milli_c) {
    Tc1047Sample sample;
    BoardStatus status;

    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = tc1047_read(&sample);
    if (status != BOARD_OK) {
        return status;
    }

    *temperature_milli_c = sample.temperature_milli_c;

    return BOARD_OK;
}
