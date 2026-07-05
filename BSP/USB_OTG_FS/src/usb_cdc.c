#include "usb_cdc.h"

#include "board_config.h"
#include "gpio.h"
#include "stm32l496xx.h"
#include "usb_app.h"

#ifndef NATALIA_USB_CLOCK_SOURCE_HSI48_CRS
#define NATALIA_USB_CLOCK_SOURCE_HSI48_CRS 0
#endif

#ifndef NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1
#define NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1 0
#endif

#if ((NATALIA_USB_CLOCK_SOURCE_HSI48_CRS + NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1) != 1)
#error "Select exactly one USB clock source"
#endif

#ifndef RCC_CCIPR_CLK48SEL_Pos
#define RCC_CCIPR_CLK48SEL_Pos 26U
#endif

#ifndef RCC_CCIPR_CLK48SEL_Msk
#define RCC_CCIPR_CLK48SEL_Msk (3UL << RCC_CCIPR_CLK48SEL_Pos)
#endif

#ifndef RCC_PLLSAI1CFGR_PLLSAI1N_Pos
#define RCC_PLLSAI1CFGR_PLLSAI1N_Pos 8U
#endif

#ifndef RCC_PLLSAI1CFGR_PLLSAI1N_Msk
#define RCC_PLLSAI1CFGR_PLLSAI1N_Msk (127UL << RCC_PLLSAI1CFGR_PLLSAI1N_Pos)
#endif

#ifndef RCC_PLLSAI1CFGR_PLLSAI1Q_Pos
#define RCC_PLLSAI1CFGR_PLLSAI1Q_Pos 21U
#endif

#ifndef RCC_PLLSAI1CFGR_PLLSAI1Q_Msk
#define RCC_PLLSAI1CFGR_PLLSAI1Q_Msk (3UL << RCC_PLLSAI1CFGR_PLLSAI1Q_Pos)
#endif

#ifndef RCC_PLLSAI1CFGR_PLLSAI1QEN
#define RCC_PLLSAI1CFGR_PLLSAI1QEN (1UL << 20U)
#endif

#define USB_CDC_WAIT_TIMEOUT_ITERATIONS 1000000UL
#define USB_CDC_CLK48SEL_HSI48 0UL
#define USB_CDC_CLK48SEL_PLLSAI1Q 1UL
#define USB_CDC_PLLSAI1N_VALUE 24UL
#define USB_CDC_PLLSAI1Q_REGISTER_VALUE 1UL
#define USB_CDC_WRITE_CHUNK_SIZE 64U

static uint8_t usb_cdc_initialized;

static BoardStatus usb_cdc_wait_flag_set(volatile uint32_t* reg,
                                         uint32_t mask) {
    uint32_t timeout = USB_CDC_WAIT_TIMEOUT_ITERATIONS;

    while ((*reg & mask) == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus usb_cdc_wait_flag_clear(volatile uint32_t* reg,
                                           uint32_t mask) {
    uint32_t timeout = USB_CDC_WAIT_TIMEOUT_ITERATIONS;

    while ((*reg & mask) != 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static void usb_cdc_select_clk48(uint32_t source) {
    RCC->CCIPR &= ~RCC_CCIPR_CLK48SEL_Msk;
    RCC->CCIPR |= (source << RCC_CCIPR_CLK48SEL_Pos) &
        RCC_CCIPR_CLK48SEL_Msk;
}

#if (NATALIA_USB_CLOCK_SOURCE_HSI48_CRS != 0)

static BoardStatus usb_cdc_clock_init_hsi48_crs(void) {
    BoardStatus status;

    RCC->APB1ENR1 |= RCC_APB1ENR1_CRSEN;
    (void)RCC->APB1ENR1;

    RCC->CRRCR |= RCC_CRRCR_HSI48ON;

    status = usb_cdc_wait_flag_set(&RCC->CRRCR, RCC_CRRCR_HSI48RDY);
    if (status != BOARD_OK) {
        return status;
    }

    usb_cdc_select_clk48(USB_CDC_CLK48SEL_HSI48);

    CRS->CR |= CRS_CR_AUTOTRIMEN | CRS_CR_CEN;

    return BOARD_OK;
}

#endif

#if (NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1 != 0)

#if (BOARD_HSE_FREQUENCY_HZ != 8000000UL)
#error "HSE8 PLLSAI1 USB clock configuration expects BOARD_HSE_FREQUENCY_HZ=8000000"
#endif

static BoardStatus usb_cdc_enable_hse_if_needed(void) {

#if ((BOARD_CLOCK_SOURCE != BOARD_CLOCK_SOURCE_HSE_CRYSTAL) && \
     (BOARD_CLOCK_SOURCE != BOARD_CLOCK_SOURCE_HSE_BYPASS))
return BOARD_ERR_INVALID_ARG;
#else
BoardStatus status;

    if ((RCC->CR& RCC_CR_HSERDY) != 0U) {
        return BOARD_OK;
    }

RCC->CR&= ~RCC_CR_HSEON;

status = usb_cdc_wait_flag_clear(&RCC->CR, RCC_CR_HSERDY);
    if (status!= BOARD_OK) {
        return status;
    }

#if (BOARD_CLOCK_SOURCE == BOARD_CLOCK_SOURCE_HSE_BYPASS)
RCC->CR|= RCC_CR_HSEBYP;
#else
RCC->CR&= ~RCC_CR_HSEBYP;
#endif

RCC->CR|= RCC_CR_HSEON;

    return usb_cdc_wait_flag_set(&RCC->CR, RCC_CR_HSERDY);
#endif
}

static BoardStatus usb_cdc_clock_init_hse8_pllsai1(void) {
    BoardStatus status;
    uint32_t pllsai1cfgr;

    status = usb_cdc_enable_hse_if_needed();
    if (status != BOARD_OK) {
        return status;
    }

    if ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != RCC_PLLCFGR_PLLSRC_HSE) {
        return BOARD_ERR_INVALID_ARG;
    }

    RCC->CR &= ~RCC_CR_PLLSAI1ON;

    status = usb_cdc_wait_flag_clear(&RCC->CR, RCC_CR_PLLSAI1RDY);
    if (status != BOARD_OK) {
        return status;
    }

    pllsai1cfgr =
        (USB_CDC_PLLSAI1N_VALUE << RCC_PLLSAI1CFGR_PLLSAI1N_Pos) |
        (USB_CDC_PLLSAI1Q_REGISTER_VALUE << RCC_PLLSAI1CFGR_PLLSAI1Q_Pos) |
        RCC_PLLSAI1CFGR_PLLSAI1QEN;

    pllsai1cfgr &= RCC_PLLSAI1CFGR_PLLSAI1N_Msk |
        RCC_PLLSAI1CFGR_PLLSAI1Q_Msk |
        RCC_PLLSAI1CFGR_PLLSAI1QEN;

    RCC->PLLSAI1CFGR = pllsai1cfgr;

    RCC->CR |= RCC_CR_PLLSAI1ON;

    status = usb_cdc_wait_flag_set(&RCC->CR, RCC_CR_PLLSAI1RDY);
    if (status != BOARD_OK) {
        return status;
    }

    usb_cdc_select_clk48(USB_CDC_CLK48SEL_PLLSAI1Q);

    return BOARD_OK;
}

#endif

static BoardStatus usb_cdc_clock_init(void) {
#if (NATALIA_USB_CLOCK_SOURCE_HSI48_CRS != 0)
    return usb_cdc_clock_init_hsi48_crs();
#elif (NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1 != 0)
    return usb_cdc_clock_init_hse8_pllsai1();
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

static BoardStatus usb_cdc_gpio_init(void) {
    static const GpioConfig usb_pin_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_VERY_HIGH,
        .initial_level = GPIO_LEVEL_LOW
    };

    BoardStatus status;

    status = gpio_configure(BOARD_PIN_USB_DM, &usb_pin_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_USB_DP, &usb_pin_config);
}

static BoardStatus usb_cdc_peripheral_enable(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;

    PWR->CR2 |= PWR_CR2_USV;
    (void)PWR->CR2;

    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;

    return BOARD_OK;
}

static void usb_cdc_peripheral_disable(void) {
    RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
}

BoardStatus usb_cdc_init(void) {
    BoardStatus status;

    if (usb_cdc_initialized != 0U) {
        return BOARD_OK;
    }

    status = usb_cdc_clock_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = usb_cdc_gpio_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = usb_cdc_peripheral_enable();
    if (status != BOARD_OK) {
        return status;
    }

    USBapp_Init();

    usb_cdc_initialized = 1U;

    return BOARD_OK;
}

BoardStatus usb_cdc_deinit(void) {
    BoardStatus status;
    BoardStatus result = BOARD_OK;

    if (usb_cdc_initialized == 0U) {
        return BOARD_OK;
    }

    USBapp_DeInit();

    status = gpio_set_disconnected(BOARD_PIN_USB_DM);
    if (status != BOARD_OK) {
        result = status;
    }

    status = gpio_set_disconnected(BOARD_PIN_USB_DP);
    if ((status != BOARD_OK) && (result == BOARD_OK)) {
        result = status;
    }

    usb_cdc_peripheral_disable();

#if (NATALIA_USB_CLOCK_SOURCE_HSI48_CRS != 0)
    CRS->CR &= ~(CRS_CR_AUTOTRIMEN | CRS_CR_CEN);
    RCC->CRRCR &= ~RCC_CRRCR_HSI48ON;
#elif (NATALIA_USB_CLOCK_SOURCE_HSE8_PLLSAI1 != 0)
    RCC->CR &= ~RCC_CR_PLLSAI1ON;
#endif

    usb_cdc_initialized = 0U;

    return result;
}

BoardStatus usb_cdc_write(const void* buffer, size_t size, size_t* bytes_written) {
    const uint8_t* source;
    size_t total_written = 0U;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_written == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = 0U;

    if (size == 0U) {
        return BOARD_OK;
    }

    if (usb_cdc_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    if (USBapp_CdcIsReady() == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    source = buffer;

    while (total_written < size) {
        size_t remaining = size - total_written;
        uint16_t chunk_size;

        if (USBapp_CdcIsReady() == 0U) {
            *bytes_written = total_written;
            return BOARD_ERR_NOT_READY;
        }

        chunk_size = (remaining > USB_CDC_WRITE_CHUNK_SIZE)
                         ? (uint16_t)USB_CDC_WRITE_CHUNK_SIZE
                         : (uint16_t)remaining;

        vcom0_write(&source[total_written], chunk_size);

        total_written += chunk_size;
    }

    *bytes_written = total_written;

    return BOARD_OK;
}

BoardStatus usb_cdc_is_ready(uint8_t* is_ready) {
    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (usb_cdc_initialized == 0U) {
        *is_ready = 0U;
        return BOARD_OK;
    }

    *is_ready = USBapp_CdcIsReady();

    return BOARD_OK;
}
