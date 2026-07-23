#include "adc.h"

#include <stdint.h>

#include "board_pins.h"
#include "gpio.h"
#include "stm32l496xx.h"

#define ADC_CHANNEL_COUNT (2UL)
#define ADC_DMA_BUFFER_LENGTH (ADC_CHANNEL_COUNT)

#define ADC_TERM_A_FRAME_OFFSET (0UL)
#define ADC_VREFINT_FRAME_OFFSET (1UL)

#define ADC_TERM_A_CHANNEL (5UL)
#define ADC_VREFINT_CHANNEL (0UL)

#define ADC_TIMEOUT (1000000UL)
#define ADC_SAMPLE_TIME_640_5_CYCLES (7UL)
#define ADC_MAX_RAW_VALUE (4095UL)

#ifndef ADC_DMA_REQUEST_ID
#define ADC_DMA_REQUEST_ID (0UL)
#endif

typedef enum {
    ADC_STATE_IDLE = 0,
    ADC_STATE_BUSY,
    ADC_STATE_READY,
    ADC_STATE_ERROR
} AdcState;

static volatile uint16_t adc_dma_buffer[ADC_DMA_BUFFER_LENGTH];
static uint8_t adc_initialized;
static uint8_t adc_powered;
static volatile uint8_t adc_ready;
static volatile uint8_t adc_state;
static volatile uint32_t adc_sequence;

void DMA1_Channel1_IRQHandler(void);

static BoardStatus adc_wait_flag_set(volatile uint32_t* reg, uint32_t mask) {
    uint32_t timeout;

    if (reg == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    timeout = ADC_TIMEOUT;

    while ((*reg & mask) == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus adc_wait_flag_clear(volatile uint32_t* reg, uint32_t mask) {
    uint32_t timeout;

    if (reg == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    timeout = ADC_TIMEOUT;

    while ((*reg & mask) != 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static void adc_delay_cycles(uint32_t cycles) {
    while (cycles > 0U) {
        __NOP();
        --cycles;
    }
}

static BoardStatus adc_configure_term_a_pin(void) {
    static const GpioConfig config = {
        .mode = GPIO_MODE_ANALOG,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    return gpio_configure(BOARD_PIN_ADC12_IN5, &config);
}

static void adc_enable_dma_clock(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;
}

static void adc_enable_dma_irq(void) {
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
}

#if defined(NATALIA_ADC_USE_VREFBUF) && (NATALIA_ADC_USE_VREFBUF != 0)
static BoardStatus adc_enable_vrefbuf(void) {
    VREFBUF->CSR &= ~VREFBUF_CSR_VRS;
    VREFBUF->CSR &= ~VREFBUF_CSR_HIZ;
    VREFBUF->CSR |= VREFBUF_CSR_ENVR;

    return adc_wait_flag_set(&VREFBUF->CSR, VREFBUF_CSR_VRR);
}
#endif

static BoardStatus adc_enable_adc_clock_and_reference(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_ADCEN;
    (void)RCC->AHB2ENR;

    ADC123_COMMON->CCR &= ~ADC_CCR_CKMODE;
    ADC123_COMMON->CCR |= ADC_CCR_CKMODE_0 | ADC_CCR_CKMODE_1;
    ADC123_COMMON->CCR |= ADC_CCR_VREFEN;

    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;

    adc_delay_cycles(10000UL);

#if defined(NATALIA_ADC_USE_VREFBUF) && (NATALIA_ADC_USE_VREFBUF != 0)
    {
        BoardStatus vrefbuf_status = adc_enable_vrefbuf();
        if (vrefbuf_status != BOARD_OK) {
            return vrefbuf_status;
        }
    }
#endif

    adc_powered = 1U;

    return BOARD_OK;
}

static BoardStatus adc_stop_conversion_if_active(void) {
    BoardStatus status;

    if ((ADC1->CR & ADC_CR_ADSTART) == 0U) {
        return BOARD_OK;
    }

    ADC1->CR |= ADC_CR_ADSTP;

    status = adc_wait_flag_clear(&ADC1->CR, ADC_CR_ADSTART);
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}

static BoardStatus adc_disable_if_enabled(void) {
    BoardStatus status;

    status = adc_stop_conversion_if_active();
    if (status != BOARD_OK) {
        return status;
    }

    if ((ADC1->CR & ADC_CR_ADEN) == 0U) {
        return BOARD_OK;
    }

    ADC1->CR |= ADC_CR_ADDIS;

    status = adc_wait_flag_clear(&ADC1->CR, ADC_CR_ADEN);
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}

static BoardStatus adc_power_down(void) {
    BoardStatus status;

    if (adc_powered == 0U) {
        return BOARD_OK;
    }

    status = adc_disable_if_enabled();
    if (status != BOARD_OK) {
        return status;
    }

    ADC123_COMMON->CCR &= ~ADC_CCR_VREFEN;

    ADC1->CR &= ~ADC_CR_ADVREGEN;
    ADC1->CR |= ADC_CR_DEEPPWD;

    RCC->AHB2ENR &= ~RCC_AHB2ENR_ADCEN;
    (void)RCC->AHB2ENR;

    adc_powered = 0U;

    return BOARD_OK;
}

static BoardStatus adc_calibrate(void) {
    ADC1->CR |= ADC_CR_ADCAL;

    return adc_wait_flag_clear(&ADC1->CR, ADC_CR_ADCAL);
}

static BoardStatus adc_enable_adc(void) {
    BoardStatus status;

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;

    status = adc_wait_flag_set(&ADC1->ISR, ADC_ISR_ADRDY);
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}

static BoardStatus adc_set_sample_time(uint32_t channel) {
    uint32_t shift;
    uint32_t mask;

    if (channel > 9UL) {
        return BOARD_ERR_INVALID_ARG;
    }

    shift = channel * 3UL;
    mask = 7UL << shift;

    ADC1->SMPR1 &= ~mask;
    ADC1->SMPR1 |= ADC_SAMPLE_TIME_640_5_CYCLES << shift;

    return BOARD_OK;
}

static BoardStatus adc_configure_regular_sequence(void) {
    BoardStatus status;

    ADC1->DIFSEL = 0U;

    ADC1->CFGR = ADC_CFGR_DMAEN |
        ADC_CFGR_OVRMOD;

    ADC1->IER = 0U;

    status = adc_set_sample_time(ADC_TERM_A_CHANNEL);
    if (status != BOARD_OK) {
        return status;
    }

    status = adc_set_sample_time(ADC_VREFINT_CHANNEL);
    if (status != BOARD_OK) {
        return status;
    }

    ADC1->SQR1 =
        (1UL << ADC_SQR1_L_Pos) |
        (ADC_TERM_A_CHANNEL << ADC_SQR1_SQ1_Pos) |
        (ADC_VREFINT_CHANNEL << ADC_SQR1_SQ2_Pos);

    return BOARD_OK;
}

static BoardStatus adc_configure_dma(void) {
    if ((DMA1_Channel1->CCR & DMA_CCR_EN) != 0U) {
        DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    }

#if defined(DMA_CSELR_C1S)
    DMA1_CSELR->CSELR &= ~DMA_CSELR_C1S;
    DMA1_CSELR->CSELR |= ADC_DMA_REQUEST_ID << DMA_CSELR_C1S_Pos;
#endif

    DMA1->IFCR = DMA_IFCR_CGIF1;

    DMA1_Channel1->CPAR = (uint32_t)(uintptr_t)&ADC1->DR;
    DMA1_Channel1->CMAR = (uint32_t)(uintptr_t)&adc_dma_buffer[0];
    DMA1_Channel1->CNDTR = ADC_DMA_BUFFER_LENGTH;

    DMA1_Channel1->CCR =
        DMA_CCR_MINC |
        DMA_CCR_PSIZE_0 |
        DMA_CCR_MSIZE_0 |
        DMA_CCR_PL_1;

    return BOARD_OK;
}

static BoardStatus adc_handle_dma_flags(void) {
    uint32_t isr;

    isr = DMA1->ISR;

    if ((isr & DMA_ISR_TEIF1) != 0U) {
        DMA1_Channel1->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CTEIF1 | DMA_IFCR_CGIF1;
        adc_ready = 0U;
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return BOARD_ERR_IO;
    }

    if ((isr & DMA_ISR_TCIF1) != 0U) {
        DMA1_Channel1->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CTCIF1 | DMA_IFCR_CHTIF1 | DMA_IFCR_CGIF1;

        adc_ready = 1U;
        adc_state = (uint8_t)ADC_STATE_READY;

        if (adc_sequence != UINT32_MAX) {
            ++adc_sequence;
        }
    }

    return BOARD_OK;
}

static BoardStatus adc_compute_vdda_mv(uint16_t vrefint_raw, uint32_t* vdda_mv) {
    const volatile uint16_t* vrefint_cal;
    uint32_t calibration;
    uint32_t calibration_vref_mv;
    uint64_t numerator;

    if (vdda_mv == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (vrefint_raw == 0U) {
        return BOARD_ERR_IO;
    }

#if defined(VREFINT_CAL_ADDR)
    vrefint_cal = (const volatile uint16_t*)VREFINT_CAL_ADDR;
#else
    vrefint_cal = (const volatile uint16_t*)0x1FFF75AAUL;
#endif

#if defined(VREFINT_CAL_VREF)
    calibration_vref_mv = (uint32_t)VREFINT_CAL_VREF;
#else
    calibration_vref_mv = 3000UL;
#endif

    calibration = (uint32_t)(*vrefint_cal);

    if (calibration == 0UL) {
        return BOARD_ERR_IO;
    }

    numerator = (uint64_t)calibration_vref_mv * (uint64_t)calibration;
    numerator += (uint64_t)vrefint_raw / 2ULL;

    *vdda_mv = (uint32_t)(numerator / (uint64_t)vrefint_raw);

    return BOARD_OK;
}

static uint32_t adc_raw_to_mv(uint16_t raw, uint32_t vdda_mv) {
    uint64_t numerator;

    numerator = (uint64_t)raw * (uint64_t)vdda_mv;
    numerator += ADC_MAX_RAW_VALUE / 2ULL;

    return (uint32_t)(numerator / ADC_MAX_RAW_VALUE);
}

BoardStatus adc_init(void) {
    BoardStatus status;

    adc_initialized = 0U;
    adc_powered = 0U;
    adc_ready = 0U;
    adc_state = (uint8_t)ADC_STATE_IDLE;
    adc_sequence = 0U;
    adc_dma_buffer[ADC_TERM_A_FRAME_OFFSET] = 0U;
    adc_dma_buffer[ADC_VREFINT_FRAME_OFFSET] = 0U;

    status = adc_configure_term_a_pin();
    if (status != BOARD_OK) {
        return status;
    }

    adc_enable_dma_clock();
    adc_enable_dma_irq();

    adc_initialized = 1U;

    return BOARD_OK;
}

BoardStatus adc_start_sample(void) {
    BoardStatus status;

    if (adc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    if (adc_state == (uint8_t)ADC_STATE_BUSY) {
        return BOARD_ERR_BUSY;
    }

    adc_ready = 0U;
    adc_state = (uint8_t)ADC_STATE_IDLE;
    adc_dma_buffer[ADC_TERM_A_FRAME_OFFSET] = 0U;
    adc_dma_buffer[ADC_VREFINT_FRAME_OFFSET] = 0U;

    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF1;

    status = adc_power_down();
    if (status != BOARD_OK) {
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    status = adc_enable_adc_clock_and_reference();
    if (status != BOARD_OK) {
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    status = adc_calibrate();
    if (status != BOARD_OK) {
        (void)adc_power_down();
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    status = adc_configure_regular_sequence();
    if (status != BOARD_OK) {
        (void)adc_power_down();
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    status = adc_configure_dma();
    if (status != BOARD_OK) {
        (void)adc_power_down();
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    status = adc_enable_adc();
    if (status != BOARD_OK) {
        (void)adc_power_down();
        adc_state = (uint8_t)ADC_STATE_ERROR;
        return status;
    }

    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR | ADC_ISR_ADRDY;

    adc_state = (uint8_t)ADC_STATE_BUSY;

    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->CR |= ADC_CR_ADSTART;

    return BOARD_OK;
}

BoardStatus adc_stop(void) {
    BoardStatus status;

    if (adc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF1;

    status = adc_power_down();

    adc_ready = 0U;
    adc_state = (uint8_t)ADC_STATE_IDLE;

    return status;
}

BoardStatus adc_is_busy(uint8_t* is_busy) {
    if (is_busy == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (adc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    (void)adc_handle_dma_flags();

    if (adc_state == (uint8_t)ADC_STATE_BUSY) {
        *is_busy = 1U;
    } else {
        *is_busy = 0U;
    }

    return BOARD_OK;
}

BoardStatus adc_has_ready_sample(uint8_t* has_sample) {
    if (has_sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (adc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    (void)adc_handle_dma_flags();

    *has_sample = adc_ready;

    return BOARD_OK;
}

BoardStatus adc_read_input(AdcInputId input, AdcSample* sample) {
    uint16_t raw;
    uint16_t vrefint_raw;
    uint32_t sequence;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (input != ADC_INPUT_TERM_A) {
        return BOARD_ERR_UNSUPPORTED;
    }

    if (adc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = adc_handle_dma_flags();
    if (status != BOARD_OK) {
        (void)adc_power_down();
        return status;
    }

    if (adc_state == (uint8_t)ADC_STATE_BUSY) {
        return BOARD_ERR_BUSY;
    }

    if (adc_state == (uint8_t)ADC_STATE_ERROR) {
        (void)adc_power_down();
        return BOARD_ERR_IO;
    }

    if (adc_ready == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    raw = adc_dma_buffer[ADC_TERM_A_FRAME_OFFSET];
    vrefint_raw = adc_dma_buffer[ADC_VREFINT_FRAME_OFFSET];
    sequence = adc_sequence;

    sample->raw = raw;
    sample->vrefint_raw = vrefint_raw;
    sample->sequence = sequence;
    sample->ready = 1U;

    status = adc_compute_vdda_mv(vrefint_raw, &sample->vdda_mv);
    if (status != BOARD_OK) {
        (void)adc_power_down();
        return status;
    }

    sample->millivolts = adc_raw_to_mv(raw, sample->vdda_mv);

    status = adc_power_down();
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}

void DMA1_Channel1_IRQHandler(void) {
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF1;
}
