#include "spi.h"

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "gpio.h"
#include "stm32l496xx.h"

#define SPI_TIMEOUT (1000000UL)

#ifndef NATALIA_SPI_MRAM_BR
#define NATALIA_SPI_MRAM_BR (0UL)
#endif

#ifndef SPI_DMA_READ_ENABLED
#define SPI_DMA_READ_ENABLED 0
#endif

#ifndef SPI_DMA_WRITE_ENABLED
#define SPI_DMA_WRITE_ENABLED 0
#endif

#ifndef SPI_DMA_AUTO_MIN_SIZE
#define SPI_DMA_AUTO_MIN_SIZE 16U
#endif

#define SPI_DMA_ENABLED (SPI_DMA_READ_ENABLED || SPI_DMA_WRITE_ENABLED)
#define SPI_DMA_MAX_COUNT 0xFFFFU

typedef struct {
    SPI_TypeDef* instance;
    BoardPinId sck_pin;
    BoardPinId miso_pin;
    BoardPinId mosi_pin;
    BoardPinId nss_pin;
    volatile uint32_t* enable_reg;
    uint32_t enable_mask;
    volatile uint32_t* reset_reg;
    uint32_t reset_mask;
    uint8_t initialized;
} SpiBusState;

static SpiBusState spi_bus_states[] = {
    [SPI_BUS_MRAM1] = {
        .instance = SPI1,
        .sck_pin = BOARD_PIN_SPI1_SCK,
        .miso_pin = BOARD_PIN_SPI1_MISO,
        .mosi_pin = BOARD_PIN_SPI1_MOSI,
        .nss_pin = BOARD_PIN_SPI1_NSS,
        .enable_reg = &RCC->APB2ENR,
        .enable_mask = RCC_APB2ENR_SPI1EN,
        .reset_reg = &RCC->APB2RSTR,
        .reset_mask = RCC_APB2RSTR_SPI1RST,
        .initialized = 0U
    },
    [SPI_BUS_MRAM2] = {
        .instance = SPI3,
        .sck_pin = BOARD_PIN_SPI3_SCK,
        .miso_pin = BOARD_PIN_SPI3_MISO,
        .mosi_pin = BOARD_PIN_SPI3_MOSI,
        .nss_pin = BOARD_PIN_SPI3_NSS,
        .enable_reg = &RCC->APB1ENR1,
        .enable_mask = RCC_APB1ENR1_SPI3EN,
        .reset_reg = &RCC->APB1RSTR1,
        .reset_mask = RCC_APB1RSTR1_SPI3RST,
        .initialized = 0U
    }
};

#if SPI_DMA_ENABLED

typedef struct {
    DMA_TypeDef* dma;
    DMA_Channel_TypeDef* rx_channel;
    DMA_Channel_TypeDef* tx_channel;
    DMA_Request_TypeDef* cselr;
    uint32_t rx_channel_index;
    uint32_t tx_channel_index;
    uint32_t rx_cselr_shift;
    uint32_t tx_cselr_shift;
    uint32_t cselr_request;
    volatile uint32_t* clock_reg;
    uint32_t clock_mask;
} SpiDmaConfig;

static const SpiDmaConfig spi_dma_configs[] = {
    [SPI_BUS_MRAM1] = {
        .dma = DMA1,
        .rx_channel = DMA1_Channel2,
        .tx_channel = DMA1_Channel3,
        .cselr = DMA1_CSELR,
        .rx_channel_index = 2U,
        .tx_channel_index = 3U,
        .rx_cselr_shift = 4U,
        .tx_cselr_shift = 8U,
        .cselr_request = 1U,
        .clock_reg = &RCC->AHB1ENR,
        .clock_mask = RCC_AHB1ENR_DMA1EN
    },
    [SPI_BUS_MRAM2] = {
        .dma = DMA2,
        .rx_channel = DMA2_Channel1,
        .tx_channel = DMA2_Channel2,
        .cselr = DMA2_CSELR,
        .rx_channel_index = 1U,
        .tx_channel_index = 2U,
        .rx_cselr_shift = 0U,
        .tx_cselr_shift = 4U,
        .cselr_request = 3U,
        .clock_reg = &RCC->AHB1ENR,
        .clock_mask = RCC_AHB1ENR_DMA2EN
    }
};

#define SPI_DMA_TCIF(index) ((uint32_t)((uint32_t)DMA_ISR_TCIF1 << (4U * ((index) - 1U))))
#define SPI_DMA_TEIF(index) ((uint32_t)((uint32_t)DMA_ISR_TEIF1 << (4U * ((index) - 1U))))
#define SPI_DMA_FLAGS(index) ((uint32_t)(0xFUL << (4U * ((index) - 1U))))

#endif /* SPI_DMA_ENABLED */

static BoardStatus spi_get_state(SpiBusId bus, SpiBusState** state) {
    if (state == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((uint32_t)bus >= (uint32_t)(sizeof(spi_bus_states) / sizeof(spi_bus_states[0]))) {
        return BOARD_ERR_INVALID_ARG;
    }

    *state = &spi_bus_states[bus];

    return BOARD_OK;
}

static BoardStatus spi_wait_flag_set(SPI_TypeDef* spi, uint32_t flag) {
    uint32_t timeout = SPI_TIMEOUT;

    while ((spi->SR & flag) == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus spi_wait_flag_clear(SPI_TypeDef* spi, uint32_t flag) {
    uint32_t timeout = SPI_TIMEOUT;

    while ((spi->SR & flag) != 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus spi_configure_pins(const SpiBusState* state) {
    GpioConfig signal_config;
    GpioConfig nss_config;
    BoardStatus status;

    signal_config.mode = GPIO_MODE_ALTERNATE;
    signal_config.pull = GPIO_PULL_NONE;
    signal_config.output_type = GPIO_OUTPUT_PUSH_PULL;
    signal_config.speed = GPIO_SPEED_HIGH;
    signal_config.initial_level = GPIO_LEVEL_LOW;

    status = gpio_configure(state->sck_pin, &signal_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(state->miso_pin, &signal_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = gpio_configure(state->mosi_pin, &signal_config);
    if (status != BOARD_OK) {
        return status;
    }

    nss_config.mode = GPIO_MODE_OUTPUT;
    nss_config.pull = GPIO_PULL_UP;
    nss_config.output_type = GPIO_OUTPUT_PUSH_PULL;
    nss_config.speed = GPIO_SPEED_HIGH;
    nss_config.initial_level = GPIO_LEVEL_HIGH;

    return gpio_configure(state->nss_pin, &nss_config);
}

BoardStatus spi_init_bus(SpiBusId bus) {
    SpiBusState* state;
    BoardStatus status;
    uint32_t cr1;

    status = spi_get_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_configure_pins(state);
    if (status != BOARD_OK) {
        return status;
    }

    *state->enable_reg |= state->enable_mask;
    (void)*state->enable_reg;

    *state->reset_reg |= state->reset_mask;
    *state->reset_reg &= ~state->reset_mask;

#if SPI_DMA_ENABLED
    *spi_dma_configs[bus].clock_reg |= spi_dma_configs[bus].clock_mask;
    (void)*spi_dma_configs[bus].clock_reg;
#endif

    cr1 = SPI_CR1_MSTR |
        SPI_CR1_SSM |
        SPI_CR1_SSI |
        ((uint32_t)NATALIA_SPI_MRAM_BR << SPI_CR1_BR_Pos);

    state->instance->CR1 = cr1;

    state->instance->CR2 =
        SPI_CR2_DS_0 | SPI_CR2_DS_1 | SPI_CR2_DS_2 |
        SPI_CR2_FRXTH;

    state->instance->CR1 |= SPI_CR1_SPE;

    state->initialized = 1U;

    return BOARD_OK;
}

BoardStatus spi_deinit_bus(SpiBusId bus) {
    SpiBusState* state;
    BoardStatus status;

    status = spi_get_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    state->instance->CR1 &= ~SPI_CR1_SPE;

    *state->reset_reg |= state->reset_mask;
    *state->reset_reg &= ~state->reset_mask;

    *state->enable_reg &= ~state->enable_mask;
    (void)*state->enable_reg;

    (void)gpio_set_disconnected(state->sck_pin);
    (void)gpio_set_disconnected(state->miso_pin);
    (void)gpio_set_disconnected(state->mosi_pin);

    state->initialized = 0U;

    return BOARD_OK;
}

BoardStatus spi_cs_assert(SpiBusId bus) {
    SpiBusState* state;
    BoardStatus status;

    status = spi_get_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    return gpio_write(state->nss_pin, GPIO_LEVEL_LOW);
}

BoardStatus spi_cs_release(SpiBusId bus) {
    SpiBusState* state;
    BoardStatus status;

    status = spi_get_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = spi_wait_flag_clear(state->instance, SPI_SR_BSY);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_write(state->nss_pin, GPIO_LEVEL_HIGH);
}

#if SPI_DMA_ENABLED
static uint8_t spi_dma_tx_dummy = 0xFFU;
static uint8_t spi_dma_rx_dummy;

static BoardStatus spi_transfer_dma(SpiBusId bus,
                                    SpiBusState* state,
                                    const uint8_t* tx,
                                    uint8_t* rx,
                                    size_t size) {
    const SpiDmaConfig* cfg = &spi_dma_configs[bus];
    SPI_TypeDef* spi = state->instance;
    uint32_t done_flags;
    uint32_t timeout;
    BoardStatus status = BOARD_OK;

    cfg->rx_channel->CCR &= ~DMA_CCR_EN;
    cfg->tx_channel->CCR &= ~DMA_CCR_EN;

    cfg->dma->IFCR = SPI_DMA_FLAGS(cfg->rx_channel_index);
    cfg->dma->IFCR = SPI_DMA_FLAGS(cfg->tx_channel_index);

    cfg->cselr->CSELR &= ~(0xFUL << cfg->rx_cselr_shift);
    cfg->cselr->CSELR |= (cfg->cselr_request << cfg->rx_cselr_shift);
    cfg->cselr->CSELR &= ~(0xFUL << cfg->tx_cselr_shift);
    cfg->cselr->CSELR |= (cfg->cselr_request << cfg->tx_cselr_shift);

    cfg->rx_channel->CPAR = (uint32_t)(uintptr_t)&spi->DR;
    if (rx != 0) {
        cfg->rx_channel->CMAR = (uint32_t)(uintptr_t)rx;
        cfg->rx_channel->CCR = DMA_CCR_MINC | DMA_CCR_PL_1;
    } else {
        cfg->rx_channel->CMAR = (uint32_t)(uintptr_t)&spi_dma_rx_dummy;
        cfg->rx_channel->CCR = DMA_CCR_PL_1;
    }
    cfg->rx_channel->CNDTR = (uint32_t)size;

    cfg->tx_channel->CPAR = (uint32_t)(uintptr_t)&spi->DR;
    if (tx != 0) {
        cfg->tx_channel->CMAR = (uint32_t)(uintptr_t)tx;
        cfg->tx_channel->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_PL_1;
    } else {
        cfg->tx_channel->CMAR = (uint32_t)(uintptr_t)&spi_dma_tx_dummy;
        cfg->tx_channel->CCR = DMA_CCR_DIR | DMA_CCR_PL_1;
    }
    cfg->tx_channel->CNDTR = (uint32_t)size;

    cfg->rx_channel->CCR |= DMA_CCR_EN;
    cfg->tx_channel->CCR |= DMA_CCR_EN;

    spi->CR2 |= SPI_CR2_RXDMAEN;
    spi->CR2 |= SPI_CR2_TXDMAEN;

    done_flags = SPI_DMA_TCIF(cfg->rx_channel_index) |
                 SPI_DMA_TEIF(cfg->rx_channel_index);
    timeout = SPI_TIMEOUT;
    while ((cfg->dma->ISR & done_flags) == 0U) {
        if (timeout == 0U) {
            status = BOARD_ERR_TIMEOUT;
            break;
        }
        --timeout;
    }

    if ((status == BOARD_OK) &&
        ((cfg->dma->ISR & SPI_DMA_TEIF(cfg->rx_channel_index)) != 0U)) {
        status = BOARD_ERR_IO;
    }

    if (status == BOARD_OK) {
        status = spi_wait_flag_clear(spi, SPI_SR_BSY);
    }

    spi->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    cfg->rx_channel->CCR &= ~DMA_CCR_EN;
    cfg->tx_channel->CCR &= ~DMA_CCR_EN;
    cfg->dma->IFCR = SPI_DMA_FLAGS(cfg->rx_channel_index);
    cfg->dma->IFCR = SPI_DMA_FLAGS(cfg->tx_channel_index);

    return status;
}
#endif /* SPI_DMA_ENABLED */

BoardStatus spi_transfer(SpiBusId bus, const uint8_t* tx, uint8_t* rx, size_t size) {
    SpiBusState* state;
    BoardStatus status;
    size_t i;

    status = spi_get_state(bus, &state);
    if (status != BOARD_OK) {
        return status;
    }

    if (state->initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    if ((size > 0U) && (tx == 0) && (rx == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

#if SPI_DMA_ENABLED
    if ((size >= (size_t)SPI_DMA_AUTO_MIN_SIZE) &&
        (size <= (size_t)SPI_DMA_MAX_COUNT)) {
        int use_dma;

        if ((rx != 0) && (tx == 0)) {
            use_dma = SPI_DMA_READ_ENABLED;
        } else if ((tx != 0) && (rx == 0)) {
            use_dma = SPI_DMA_WRITE_ENABLED;
        } else {
            use_dma = (SPI_DMA_READ_ENABLED && SPI_DMA_WRITE_ENABLED);
        }

        if (use_dma != 0) {
            return spi_transfer_dma(bus, state, tx, rx, size);
        }
    }
#endif

    for (i = 0U; i < size; ++i) {
        uint8_t out = (tx != 0) ? tx[i] : 0xFFU;
        uint8_t in;

        status = spi_wait_flag_set(state->instance, SPI_SR_TXE);
        if (status != BOARD_OK) {
            return status;
        }

        *(volatile uint8_t*)&state->instance->DR = out;

        status = spi_wait_flag_set(state->instance, SPI_SR_RXNE);
        if (status != BOARD_OK) {
            return status;
        }

        in = *(volatile uint8_t*)&state->instance->DR;
        if (rx != 0) {
            rx[i] = in;
        }
    }

    return BOARD_OK;
}
