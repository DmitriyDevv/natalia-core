#include "ftdi.h"

#include <stdint.h>
#include <string.h>

#include "board_pins.h"
#include "gpio.h"
#include "stm32l496xx.h"
#include "timebase.h"

#define FTDI_KERNEL_CLOCK_HZ (80000000UL)

/*
 * FTDI/USART1 runs at 3 Mbaud from PCLK2 (80 MHz, the system clock) with
 * oversampling by 16: BRR = round(f_ck / baud) = round(80e6 / 3e6) = 27,
 * about -1.2% baud error, comfortably within the UART tolerance.
 *
 * To change the baud, edit FTDI_BAUDRATE; BRR is recomputed from the kernel
 * clock. With OVER16 the minimum is f_ck / 65535 and the maximum is f_ck / 16
 * (= 5 Mbaud here). The connected FTDI part must support the chosen rate.
 */ 
#define FTDI_BAUDRATE (1000000UL)

#define FTDI_TX_RING_SIZE (2048U)
#define FTDI_TX_RING_MASK (FTDI_TX_RING_SIZE - 1U)

#define FTDI_DMA_REQUEST_USART1_TX (2UL)

#define FTDI_PS_ON_LEVEL      GPIO_LEVEL_HIGH
#define FTDI_PSON_READY_LEVEL GPIO_LEVEL_HIGH
#define FTDI_RES_ACTIVE_LEVEL GPIO_LEVEL_LOW
#define FTDI_RES_RUN_LEVEL    GPIO_LEVEL_HIGH
#define FTDI_RESET_PULSE_MS   (5UL)
#define FTDI_PSON_TIMEOUT_MS  (400UL)

#define FTDI_TX_POLL_TIMEOUT  (200000UL)

static uint8_t ftdi_tx_ring[FTDI_TX_RING_SIZE];
static volatile uint32_t ftdi_tx_head;
static volatile uint32_t ftdi_tx_tail;
static volatile uint32_t ftdi_tx_count;
static volatile uint32_t ftdi_tx_inflight;
static volatile uint8_t ftdi_tx_dma_active;
static volatile uint32_t ftdi_tx_error_count;
static uint32_t ftdi_log_dropped;
static uint8_t ftdi_initialized;
static uint8_t ftdi_power_ok;
static FtdiMode ftdi_mode;

void DMA1_Channel4_IRQHandler(void);

static void ftdi_start_dma_locked(void) {
    uint32_t contiguous;

    if (ftdi_tx_count == 0U) {
        ftdi_tx_dma_active = 0U;
        return;
    }

    contiguous = FTDI_TX_RING_SIZE - ftdi_tx_tail;
    if (contiguous > ftdi_tx_count) {
        contiguous = ftdi_tx_count;
    }

    DMA1_Channel4->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF4;

    DMA1_Channel4->CMAR = (uint32_t)(uintptr_t)&ftdi_tx_ring[ftdi_tx_tail];
    DMA1_Channel4->CNDTR = contiguous;

    ftdi_tx_inflight = contiguous;
    ftdi_tx_dma_active = 1U;

    DMA1_Channel4->CCR |= DMA_CCR_EN;
}

static void ftdi_release_inflight(void) {
    ftdi_tx_tail = (ftdi_tx_tail + ftdi_tx_inflight) & FTDI_TX_RING_MASK;

    if (ftdi_tx_count >= ftdi_tx_inflight) {
        ftdi_tx_count -= ftdi_tx_inflight;
    } else {
        ftdi_tx_count = 0U;
    }

    ftdi_tx_inflight = 0U;
}

static BoardStatus ftdi_configure_control_pins(void) {
    static const GpioConfig ps_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    static const GpioConfig res_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = FTDI_RES_ACTIVE_LEVEL
    };

    static const GpioConfig pson_config = {
        .mode = GPIO_MODE_INPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    BoardStatus status;

    status = gpio_configure(BOARD_PIN_PU_FTDI_PS, &ps_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(BOARD_PIN_PU_FTDI_RES, &res_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_PU_FTDI_PSON, &pson_config);
}

static BoardStatus ftdi_power_up(void) {
    GpioLevel pson;
    uint32_t start;
    BoardStatus status;

    status = gpio_write(BOARD_PIN_PU_FTDI_PS, FTDI_PS_ON_LEVEL);
    if (status != BOARD_OK) {
        return status;
    }

    ftdi_power_ok = 0U;
    start = timebase_millis();

    for (;;) {
        status = gpio_read(BOARD_PIN_PU_FTDI_PSON, &pson);
        if (status != BOARD_OK) {
            return status;
        }

        if (pson == FTDI_PSON_READY_LEVEL) {
            ftdi_power_ok = 1U;
            break;
        }

        if ((timebase_millis() - start) >= FTDI_PSON_TIMEOUT_MS) {
            break;
        }
    }

    timebase_delay_ms_blocking(FTDI_RESET_PULSE_MS);

    return gpio_write(BOARD_PIN_PU_FTDI_RES, FTDI_RES_RUN_LEVEL);
}

static BoardStatus ftdi_configure_usart_clock(void) {
    RCC->CCIPR &= ~RCC_CCIPR_USART1SEL;

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;

    return BOARD_OK;
}

static BoardStatus ftdi_configure_usart_pins(void) {
    static const GpioConfig tx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_UP,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    static const GpioConfig rx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_UP,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    static const GpioConfig cts_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .initial_level = GPIO_LEVEL_LOW
    };

    static const GpioConfig rts_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    BoardStatus status;

    status = gpio_configure(BOARD_PIN_USART1_TX, &tx_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(BOARD_PIN_USART1_RX, &rx_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(BOARD_PIN_USART1_CTS, &cts_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_USART1_RTS, &rts_config);
}

static void ftdi_configure_usart_regs(void) {
    USART1->CR1 &= ~USART_CR1_UE;

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = USART_CR3_DMAT;
    USART1->BRR = (FTDI_KERNEL_CLOCK_HZ + (FTDI_BAUDRATE / 2U)) / FTDI_BAUDRATE;

    USART1->CR1 = USART_CR1_TE | USART_CR1_RE;
    USART1->CR1 |= USART_CR1_UE;
}

static void ftdi_configure_dma(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    DMA1_Channel4->CCR &= ~DMA_CCR_EN;

#if defined(DMA_CSELR_C4S)
    DMA1_CSELR->CSELR &= ~DMA_CSELR_C4S;
    DMA1_CSELR->CSELR |= FTDI_DMA_REQUEST_USART1_TX << DMA_CSELR_C4S_Pos;
#endif

    DMA1->IFCR = DMA_IFCR_CGIF4;

    DMA1_Channel4->CPAR = (uint32_t)(uintptr_t)&USART1->TDR;
    DMA1_Channel4->CCR =
        DMA_CCR_DIR |
        DMA_CCR_MINC |
        DMA_CCR_PL_0 |
        DMA_CCR_TCIE |
        DMA_CCR_TEIE;

    NVIC_ClearPendingIRQ(DMA1_Channel4_IRQn);
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);
}

BoardStatus ftdi_init(void) {
    BoardStatus status;

    if (ftdi_initialized != 0U) {
        return BOARD_OK;
    }

    ftdi_tx_head = 0U;
    ftdi_tx_tail = 0U;
    ftdi_tx_count = 0U;
    ftdi_tx_inflight = 0U;
    ftdi_tx_dma_active = 0U;
    ftdi_tx_error_count = 0U;
    ftdi_log_dropped = 0U;
    ftdi_power_ok = 0U;
    ftdi_mode = FTDI_MODE_LOG;

    status = ftdi_configure_control_pins();
    if (status != BOARD_OK) {
        return status;
    }

    status = ftdi_power_up();
    if (status != BOARD_OK) {
        return status;
    }

    status = ftdi_configure_usart_clock();
    if (status != BOARD_OK) {
        return status;
    }

    status = ftdi_configure_usart_pins();
    if (status != BOARD_OK) {
        return status;
    }

    ftdi_configure_usart_regs();
    ftdi_configure_dma();

    ftdi_initialized = 1U;

    return BOARD_OK;
}

void ftdi_set_mode(FtdiMode mode) {
    ftdi_mode = mode;
}

FtdiMode ftdi_get_mode(void) {
    return ftdi_mode;
}

BoardStatus ftdi_write(const uint8_t *data, size_t size, size_t *accepted) {
    size_t i;

    if (accepted != NULL) {
        *accepted = 0U;
    }

    if ((data == NULL) && (size != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (ftdi_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    for (i = 0U; i < size; ++i) {
        uint32_t guard = 0U;

        while ((USART1->ISR & USART_ISR_TXE) == 0U) {
            if (++guard >= FTDI_TX_POLL_TIMEOUT) {
                if (ftdi_mode == FTDI_MODE_LOG) {
                    ftdi_log_dropped += ((uint32_t)size - (uint32_t)i);
                }
                if (accepted != NULL) {
                    *accepted = i;
                }
                return BOARD_OK;
            }
        }

        USART1->TDR = data[i];
    }

    if (accepted != NULL) {
        *accepted = size;
    }

    return BOARD_OK;
}

BoardStatus ftdi_is_tx_idle(uint8_t *idle) {
    if (idle == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (ftdi_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    *idle = ((ftdi_tx_count == 0U) &&
             (ftdi_tx_dma_active == 0U) &&
             ((USART1->ISR & USART_ISR_TC) != 0U)) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus ftdi_flush(uint32_t timeout_ms) {
    uint32_t start;

    if (ftdi_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    start = timebase_millis();

    for (;;) {
        if ((ftdi_tx_count == 0U) &&
            (ftdi_tx_dma_active == 0U) &&
            ((USART1->ISR & USART_ISR_TC) != 0U)) {
            return BOARD_OK;
        }

        if ((timebase_millis() - start) >= timeout_ms) {
            return BOARD_ERR_TIMEOUT;
        }
    }
}

uint8_t ftdi_power_present(void) {
    return ftdi_power_ok;
}

uint32_t ftdi_log_dropped_count(void) {
    return ftdi_log_dropped;
}

void DMA1_Channel4_IRQHandler(void) {
    uint32_t isr;

    isr = DMA1->ISR;

    if ((isr & DMA_ISR_TEIF4) != 0U) {
        DMA1_Channel4->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CTEIF4 | DMA_IFCR_CGIF4;
        ++ftdi_tx_error_count;
        ftdi_release_inflight();
        ftdi_start_dma_locked();
        return;
    }

    if ((isr & DMA_ISR_TCIF4) != 0U) {
        DMA1_Channel4->CCR &= ~DMA_CCR_EN;
        DMA1->IFCR = DMA_IFCR_CTCIF4 | DMA_IFCR_CHTIF4 | DMA_IFCR_CGIF4;
        ftdi_release_inflight();
        ftdi_start_dma_locked();
    }
}
