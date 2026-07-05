#include "qspi.h"

#include <stdint.h>

#include "board_config.h"
#include "qspi_private.h"
#include "stm32l496xx.h"

#define QSPI_FLASH_SIZE_BITS 28UL
#define QSPI_CHIP_SELECT_HIGH_TIME 2UL

#ifndef QSPI_TARGET_HZ
#define QSPI_TARGET_HZ 40000000UL
#endif

#ifndef QSPI_SAMPLE_SHIFT
#define QSPI_SAMPLE_SHIFT 0
#endif

#ifndef QSPI_DMA_AUTO_MIN_SIZE
#define QSPI_DMA_AUTO_MIN_SIZE 16UL
#endif

#ifndef QSPI_DMA_READ_ENABLED
#define QSPI_DMA_READ_ENABLED 0
#endif

#ifndef QSPI_DMA_WRITE_ENABLED
#define QSPI_DMA_WRITE_ENABLED 0
#endif

#define QSPI_PRESCALER_VALUE ((BOARD_HCLK_HZ / QSPI_TARGET_HZ) - 1UL)
#define QSPI_TIMEOUT_LOOPS 8000000UL
#define QSPI_DMA_MAX_TRANSFER_COUNT 0xFFFFUL

typedef enum {
    QSPI_ASYNC_IDLE = 0,
    QSPI_ASYNC_BUSY,
    QSPI_ASYNC_WAIT_TCF,
    QSPI_ASYNC_DONE
} QspiAsyncState;

typedef enum {
    QSPI_DMA_DIRECTION_READ = 0,
    QSPI_DMA_DIRECTION_WRITE = 1
} QspiDmaDirection;

static volatile QspiAsyncState qspi_dma_state = QSPI_ASYNC_IDLE;
static volatile BoardStatus qspi_dma_status = BOARD_OK;

static volatile QspiAsyncState qspi_poll_state = QSPI_ASYNC_IDLE;
static volatile BoardStatus qspi_poll_status = BOARD_OK;

static QspiDebugSnapshot qspi_dma_timeout_snapshot;
static uint8_t qspi_dma_timeout_snapshot_valid = 0U;

static BoardStatus qspi_wait_not_busy(void) {
    uint32_t timeout = QSPI_TIMEOUT_LOOPS;

    while ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus qspi_wait_flag(uint32_t flag) {
    uint32_t timeout = QSPI_TIMEOUT_LOOPS;

    while ((QUADSPI->SR & flag) == 0U) {
        if ((QUADSPI->SR & QUADSPI_SR_TEF) != 0U) {
            QUADSPI->FCR = QUADSPI_FCR_CTEF;
            return BOARD_ERR_IO;
        }

        if ((QUADSPI->SR & QUADSPI_SR_TOF) != 0U) {
            QUADSPI->FCR = QUADSPI_FCR_CTOF;
            return BOARD_ERR_TIMEOUT;
        }

        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static uint32_t qspi_fifo_level(void) {
    return (QUADSPI->SR & QUADSPI_SR_FLEVEL) >> QUADSPI_SR_FLEVEL_Pos;
}

static BoardStatus qspi_wait_fifo_not_empty(void) {
    uint32_t timeout = QSPI_TIMEOUT_LOOPS;

    while (qspi_fifo_level() == 0U) {
        if ((QUADSPI->SR & QUADSPI_SR_TEF) != 0U) {
            QUADSPI->FCR = QUADSPI_FCR_CTEF;
            return BOARD_ERR_IO;
        }

        if ((QUADSPI->SR & QUADSPI_SR_TOF) != 0U) {
            QUADSPI->FCR = QUADSPI_FCR_CTOF;
            return BOARD_ERR_TIMEOUT;
        }

        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
    }

    return BOARD_OK;
}

static void qspi_clear_flags(void) {
    QUADSPI->FCR = QUADSPI_FCR_CTCF |
        QUADSPI_FCR_CTEF |
        QUADSPI_FCR_CSMF |
        QUADSPI_FCR_CTOF;
}

static void qspi_disable_transfer_control(void) {
    QUADSPI->CR &= ~(QUADSPI_CR_DMAEN |
        QUADSPI_CR_TCIE |
        QUADSPI_CR_TEIE |
        QUADSPI_CR_SMIE |
        QUADSPI_CR_APMS);
}

static BoardStatus qspi_check_software_idle(void) {
    if (qspi_dma_state != QSPI_ASYNC_IDLE) {
        return BOARD_ERR_BUSY;
    }

    if (qspi_poll_state != QSPI_ASYNC_IDLE) {
        return BOARD_ERR_BUSY;
    }

    return BOARD_OK;
}

static uint8_t qspi_io_mode_is_valid(QspiIoMode mode) {
    return (mode == QSPI_IO_NONE) ||
        (mode == QSPI_IO_1_LINE) ||
        (mode == QSPI_IO_2_LINES) ||
        (mode == QSPI_IO_4_LINES);
}

static BoardStatus qspi_validate_command(const QspiCommand* command) {
    if (command == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (command->instruction_mode == QSPI_IO_NONE) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (qspi_io_mode_is_valid(command->instruction_mode) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (qspi_io_mode_is_valid(command->address_mode) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (qspi_io_mode_is_valid(command->alternate_mode) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (qspi_io_mode_is_valid(command->data_mode) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static uint32_t qspi_make_ccr(const QspiCommand* command, uint32_t functional_mode) {
    uint32_t ccr = 0U;

    ccr |= functional_mode << QUADSPI_CCR_FMODE_Pos;
    ccr |= ((uint32_t)command->instruction) << QUADSPI_CCR_INSTRUCTION_Pos;
    ccr |= ((uint32_t)command->instruction_mode) << QUADSPI_CCR_IMODE_Pos;

    if (command->address_mode != QSPI_IO_NONE) {
        ccr |= ((uint32_t)command->address_mode) << QUADSPI_CCR_ADMODE_Pos;
        ccr |= ((uint32_t)command->address_size) << QUADSPI_CCR_ADSIZE_Pos;
    }

    if (command->alternate_mode != QSPI_IO_NONE) {
        ccr |= ((uint32_t)command->alternate_mode) << QUADSPI_CCR_ABMODE_Pos;
        ccr |= ((uint32_t)command->alternate_size) << QUADSPI_CCR_ABSIZE_Pos;
    }

    ccr |= ((uint32_t)command->dummy_cycles) << QUADSPI_CCR_DCYC_Pos;
    ccr |= ((uint32_t)command->data_mode) << QUADSPI_CCR_DMODE_Pos;

    return ccr;
}

static BoardStatus qspi_start_command(const QspiCommand* command,
                                      uint32_t functional_mode) {
    BoardStatus status;

    status = qspi_validate_command(command);
    if (status != BOARD_OK) {
        return status;
    }

    if (command->alternate_mode != QSPI_IO_NONE) {
        QUADSPI->ABR = command->alternate;
    }

    QUADSPI->CCR = qspi_make_ccr(command, functional_mode);

    if (command->address_mode != QSPI_IO_NONE) {
        QUADSPI->AR = command->address;
    }

    return BOARD_OK;
}

static BoardStatus qspi_validate_data_command(const QspiCommand* command,
                                              const void* buffer,
                                              size_t size) {
    BoardStatus status;

    status = qspi_validate_command(command);
    if (status != BOARD_OK) {
        return status;
    }

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((size > 0U) && (command->data_mode == QSPI_IO_NONE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static uint8_t qspi_dma_transfer_is_valid(const void* buffer, size_t size) {
    if (buffer == 0) {
        return 0U;
    }

    if (((uintptr_t)buffer & 3UL) != 0UL) {
        return 0U;
    }

    if ((size & 3UL) != 0UL) {
        return 0U;
    }

    if (size == 0U) {
        return 0U;
    }

    if (size > QSPI_DMA_MAX_TRANSFER_COUNT) {
        return 0U;
    }

    return 1U;
}

static uint8_t qspi_dma_auto_can_use(const void* buffer, size_t size) {
    if (size < QSPI_DMA_AUTO_MIN_SIZE) {
        return 0U;
    }

    return qspi_dma_transfer_is_valid(buffer, size);
}

static void qspi_dma_disable_channel(void) {
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;

    while ((DMA1_Channel5->CCR & DMA_CCR_EN) != 0U) {}
}

static void qspi_dma_clear_flags(void) {
    DMA1->IFCR = DMA_IFCR_CGIF5 |
        DMA_IFCR_CTCIF5 |
        DMA_IFCR_CHTIF5 |
        DMA_IFCR_CTEIF5;
}

static void qspi_dma_configure_request(void) {
    DMA1_CSELR->CSELR &= ~(0xFUL << 16U);
    DMA1_CSELR->CSELR |= 5UL << 16U;
}

static void qspi_debug_fill_snapshot(QspiDebugSnapshot* snapshot) {
    snapshot->qspi_cr = QUADSPI->CR;
    snapshot->qspi_dcr = QUADSPI->DCR;
    snapshot->qspi_sr = QUADSPI->SR;
    snapshot->qspi_fcr = QUADSPI->FCR;
    snapshot->qspi_dlr = QUADSPI->DLR;
    snapshot->qspi_ccr = QUADSPI->CCR;
    snapshot->qspi_ar = QUADSPI->AR;
    snapshot->qspi_abr = QUADSPI->ABR;

    snapshot->dma_isr = DMA1->ISR;
    snapshot->dma_ccr = DMA1_Channel5->CCR;
    snapshot->dma_cndtr = DMA1_Channel5->CNDTR;
    snapshot->dma_cpar = DMA1_Channel5->CPAR;
    snapshot->dma_cmar = DMA1_Channel5->CMAR;
    snapshot->dma_cselr = DMA1_CSELR->CSELR;

    snapshot->dma_state = (uint32_t)qspi_dma_state;
    snapshot->dma_status = (uint32_t)qspi_dma_status;
    snapshot->poll_state = (uint32_t)qspi_poll_state;
    snapshot->poll_status = (uint32_t)qspi_poll_status;

    snapshot->qspi_flag_busy = ((snapshot->qspi_sr & QUADSPI_SR_BUSY) != 0U) ? 1U : 0U;
    snapshot->qspi_flag_tcf = ((snapshot->qspi_sr & QUADSPI_SR_TCF) != 0U) ? 1U : 0U;
    snapshot->qspi_flag_ftf = ((snapshot->qspi_sr & QUADSPI_SR_FTF) != 0U) ? 1U : 0U;
    snapshot->qspi_flag_tef = ((snapshot->qspi_sr & QUADSPI_SR_TEF) != 0U) ? 1U : 0U;
    snapshot->qspi_flag_tof = ((snapshot->qspi_sr & QUADSPI_SR_TOF) != 0U) ? 1U : 0U;
    snapshot->qspi_flag_smf = ((snapshot->qspi_sr & QUADSPI_SR_SMF) != 0U) ? 1U : 0U;
    snapshot->qspi_flevel = (snapshot->qspi_sr & QUADSPI_SR_FLEVEL) >> QUADSPI_SR_FLEVEL_Pos;

    snapshot->qspi_cr_dmaen = ((snapshot->qspi_cr & QUADSPI_CR_DMAEN) != 0U) ? 1U : 0U;
    snapshot->qspi_cr_tcie = ((snapshot->qspi_cr & QUADSPI_CR_TCIE) != 0U) ? 1U : 0U;
    snapshot->qspi_cr_teie = ((snapshot->qspi_cr & QUADSPI_CR_TEIE) != 0U) ? 1U : 0U;

    snapshot->dma_ccr_en = ((snapshot->dma_ccr & DMA_CCR_EN) != 0U) ? 1U : 0U;
    snapshot->dma_isr_tcif = ((snapshot->dma_isr & DMA_ISR_TCIF5) != 0U) ? 1U : 0U;
    snapshot->dma_isr_teif = ((snapshot->dma_isr & DMA_ISR_TEIF5) != 0U) ? 1U : 0U;
}

static void qspi_dma_capture_timeout_snapshot(void) {
    qspi_debug_fill_snapshot(&qspi_dma_timeout_snapshot);
    qspi_dma_timeout_snapshot_valid = 1U;
}

static void qspi_dma_abort(void) {
    qspi_dma_capture_timeout_snapshot();

    qspi_dma_disable_channel();

    QUADSPI->CR &= ~(QUADSPI_CR_DMAEN |
        QUADSPI_CR_TCIE |
        QUADSPI_CR_TEIE);

    QUADSPI->CR |= QUADSPI_CR_ABORT;
    (void)qspi_wait_not_busy();

    qspi_dma_clear_flags();
    qspi_clear_flags();

    qspi_dma_status = BOARD_ERR_TIMEOUT;
    qspi_dma_state = QSPI_ASYNC_IDLE;
}

static BoardStatus qspi_dma_start_transfer(const QspiCommand* command,
                                           void* buffer,
                                           size_t size,
                                           QspiDmaDirection direction) {
    BoardStatus status;
    uint32_t byte_count;
    uint32_t ccr;
    uint32_t functional_mode;

    status = qspi_validate_data_command(command, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    if (qspi_dma_transfer_is_valid(buffer, size) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    byte_count = (uint32_t)size;

    qspi_disable_transfer_control();
    qspi_clear_flags();
    qspi_dma_disable_channel();
    qspi_dma_clear_flags();

    DMA1_Channel5->CPAR = (uint32_t)(uintptr_t)&QUADSPI->DR;
    DMA1_Channel5->CMAR = (uint32_t)(uintptr_t)buffer;
    DMA1_Channel5->CNDTR = byte_count;

    ccr = DMA_CCR_MINC |
        DMA_CCR_PL_1 |
        DMA_CCR_TCIE |
        DMA_CCR_TEIE;

    if (direction == QSPI_DMA_DIRECTION_WRITE) {
        ccr |= DMA_CCR_DIR;
        functional_mode = 0U;
    } else {
        functional_mode = 1U;
    }

    DMA1_Channel5->CCR = ccr;

    QUADSPI->DLR = (uint32_t)size - 1UL;

    qspi_dma_state = QSPI_ASYNC_BUSY;
    qspi_dma_status = BOARD_OK;

    status = qspi_start_command(command, functional_mode);
    if (status != BOARD_OK) {
        qspi_dma_state = QSPI_ASYNC_IDLE;
        qspi_dma_status = status;
        return status;
    }

    DMA1_Channel5->CCR |= DMA_CCR_EN;

    QUADSPI->CR |= QUADSPI_CR_DMAEN |
        QUADSPI_CR_TCIE |
        QUADSPI_CR_TEIE;

    return BOARD_OK;
}

static void qspi_dma_process_dma_flags(void) {
    if (qspi_dma_state != QSPI_ASYNC_BUSY) {
        return;
    }

    if ((DMA1->ISR & DMA_ISR_TEIF5) != 0U) {
        qspi_dma_clear_flags();
        qspi_dma_disable_channel();

        QUADSPI->CR &= ~QUADSPI_CR_DMAEN;
        QUADSPI->CR |= QUADSPI_CR_ABORT;
        (void)qspi_wait_not_busy();

        qspi_clear_flags();

        qspi_dma_status = BOARD_ERR_IO;
        qspi_dma_state = QSPI_ASYNC_DONE;
        return;
    }

    if ((DMA1->ISR & DMA_ISR_TCIF5) != 0U) {
        qspi_dma_clear_flags();
        qspi_dma_disable_channel();
        qspi_dma_state = QSPI_ASYNC_WAIT_TCF;
    }
}

static void qspi_dma_process_qspi_flags(void) {
    if ((qspi_dma_state != QSPI_ASYNC_BUSY) &&
        (qspi_dma_state != QSPI_ASYNC_WAIT_TCF)) {
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_TEF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CTEF;

        QUADSPI->CR &= ~(QUADSPI_CR_TCIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_DMAEN);

        qspi_dma_disable_channel();

        qspi_dma_status = BOARD_ERR_IO;
        qspi_dma_state = QSPI_ASYNC_DONE;
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_TOF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CTOF;

        QUADSPI->CR &= ~(QUADSPI_CR_TCIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_DMAEN);

        qspi_dma_disable_channel();

        qspi_dma_status = BOARD_ERR_TIMEOUT;
        qspi_dma_state = QSPI_ASYNC_DONE;
        return;
    }

    if (qspi_dma_state != QSPI_ASYNC_WAIT_TCF) {
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_TCF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CTCF;

        QUADSPI->CR &= ~(QUADSPI_CR_TCIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_DMAEN);

        qspi_dma_status = BOARD_OK;
        qspi_dma_state = QSPI_ASYNC_DONE;
    }
}

static BoardStatus qspi_dma_poll_internal(uint8_t* is_done) {
    static QspiAsyncState timeout_state = QSPI_ASYNC_IDLE;
    static uint32_t timeout = 0U;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (qspi_dma_state != timeout_state) {
        timeout_state = qspi_dma_state;
        timeout = QSPI_TIMEOUT_LOOPS;
    }

    qspi_dma_process_dma_flags();
    qspi_dma_process_qspi_flags();

    if (qspi_dma_state != timeout_state) {
        timeout_state = qspi_dma_state;
        timeout = QSPI_TIMEOUT_LOOPS;
    }

    if ((qspi_dma_state == QSPI_ASYNC_BUSY) ||
        (qspi_dma_state == QSPI_ASYNC_WAIT_TCF)) {
        if (timeout == 0U) {
            qspi_dma_abort();
            timeout_state = QSPI_ASYNC_IDLE;
            *is_done = 1U;
            return BOARD_ERR_TIMEOUT;
        }

        --timeout;
        *is_done = 0U;
        return BOARD_OK;
    }

    if (qspi_dma_state == QSPI_ASYNC_DONE) {
        *is_done = 1U;
        qspi_dma_state = QSPI_ASYNC_IDLE;
        timeout_state = QSPI_ASYNC_IDLE;
        return qspi_dma_status;
    }

    *is_done = 1U;
    timeout_state = QSPI_ASYNC_IDLE;

    return qspi_dma_status;
}

static BoardStatus qspi_dma_wait_done(void) {
    BoardStatus status;
    uint8_t is_done = 0U;
    uint32_t timeout = QSPI_TIMEOUT_LOOPS;

    while (is_done == 0U) {
        if (timeout == 0U) {
            qspi_dma_abort();
            return BOARD_ERR_TIMEOUT;
        }

        status = qspi_dma_poll_internal(&is_done);
        if (status != BOARD_OK) {
            return status;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus qspi_read_cpu(const QspiCommand* command,
                                 void* buffer,
                                 size_t size) {
    uint8_t* data8 = buffer;
    uint32_t index;
    BoardStatus status;

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    qspi_disable_transfer_control();
    qspi_clear_flags();

    QUADSPI->DLR = (uint32_t)size - 1UL;

    status = qspi_start_command(command, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < (uint32_t)size; ++index) {
        status = qspi_wait_fifo_not_empty();
        if (status != BOARD_OK) {
            return status;
        }

        data8[index] = *(volatile uint8_t*)&QUADSPI->DR;
    }

    status = qspi_wait_flag(QUADSPI_SR_TCF);
    if (status != BOARD_OK) {
        return status;
    }

    qspi_clear_flags();

    return BOARD_OK;
}

static BoardStatus qspi_write_cpu(const QspiCommand* command,
                                  const void* buffer,
                                  size_t size) {
    const uint8_t* data8 = buffer;
    uint32_t index;
    BoardStatus status;

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    qspi_disable_transfer_control();
    qspi_clear_flags();

    QUADSPI->DLR = (uint32_t)size - 1UL;

    status = qspi_start_command(command, 0U);
    if (status != BOARD_OK) {
        return status;
    }

    for (index = 0U; index < (uint32_t)size; ++index) {
        status = qspi_wait_flag(QUADSPI_SR_FTF);
        if (status != BOARD_OK) {
            return status;
        }

        *(volatile uint8_t*)&QUADSPI->DR = data8[index];
    }

    status = qspi_wait_flag(QUADSPI_SR_TCF);
    if (status != BOARD_OK) {
        return status;
    }

    qspi_clear_flags();

    return BOARD_OK;
}

static void qspi_poll_process_flags(void) {
    if (qspi_poll_state != QSPI_ASYNC_BUSY) {
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_SMF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CSMF;

        QUADSPI->CR &= ~(QUADSPI_CR_SMIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_APMS);

        qspi_poll_status = BOARD_OK;
        qspi_poll_state = QSPI_ASYNC_DONE;
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_TEF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CTEF;

        QUADSPI->CR &= ~(QUADSPI_CR_SMIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_APMS);

        qspi_poll_status = BOARD_ERR_IO;
        qspi_poll_state = QSPI_ASYNC_DONE;
        return;
    }

    if ((QUADSPI->SR & QUADSPI_SR_TOF) != 0U) {
        QUADSPI->FCR = QUADSPI_FCR_CTOF;

        QUADSPI->CR &= ~(QUADSPI_CR_SMIE |
            QUADSPI_CR_TEIE |
            QUADSPI_CR_APMS);

        qspi_poll_status = BOARD_ERR_TIMEOUT;
        qspi_poll_state = QSPI_ASYNC_DONE;
    }
}


BoardStatus qspi_init(void) {
    uint32_t cr;

    RCC->AHB3ENR |= RCC_AHB3ENR_QSPIEN;
    (void)RCC->AHB3ENR;

    RCC->AHB3RSTR |= RCC_AHB3RSTR_QSPIRST;
    RCC->AHB3RSTR &= ~RCC_AHB3RSTR_QSPIRST;

    QUADSPI->CR = 0U;

    QUADSPI->DCR = (QSPI_FLASH_SIZE_BITS << QUADSPI_DCR_FSIZE_Pos) |
        (QSPI_CHIP_SELECT_HIGH_TIME << QUADSPI_DCR_CSHT_Pos);

    cr = (QSPI_PRESCALER_VALUE << QUADSPI_CR_PRESCALER_Pos) |
        (0UL << QUADSPI_CR_FTHRES_Pos);

#if (QSPI_SAMPLE_SHIFT != 0)
    cr |= QUADSPI_CR_SSHIFT;
#endif

    QUADSPI->CR = cr;
    QUADSPI->CR |= QUADSPI_CR_EN;

    qspi_dma_state = QSPI_ASYNC_IDLE;
    qspi_dma_status = BOARD_OK;
    qspi_poll_state = QSPI_ASYNC_IDLE;
    qspi_poll_status = BOARD_OK;

    return BOARD_OK;
}

BoardStatus qspi_dma_init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    qspi_dma_disable_channel();
    qspi_dma_configure_request();
    qspi_dma_clear_flags();

    DMA1_Channel5->CPAR = (uint32_t)(uintptr_t)&QUADSPI->DR;
    DMA1_Channel5->CCR = 0U;

    qspi_dma_state = QSPI_ASYNC_IDLE;
    qspi_dma_status = BOARD_OK;

    NVIC_ClearPendingIRQ(DMA1_Channel5_IRQn);
    NVIC_EnableIRQ(DMA1_Channel5_IRQn);

    NVIC_ClearPendingIRQ(QUADSPI_IRQn);
    NVIC_EnableIRQ(QUADSPI_IRQn);

    return BOARD_OK;
}

BoardStatus qspi_select_bank(QspiBank bank) {
    BoardStatus status;

    if ((bank != QSPI_BANK_1) && (bank != QSPI_BANK_2)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_configure_pins(bank);
    if (status != BOARD_OK) {
        return status;
    }

    if (bank == QSPI_BANK_1) {
        QUADSPI->CR &= ~QUADSPI_CR_FSEL;
    } else {
        QUADSPI->CR |= QUADSPI_CR_FSEL;
    }

    return BOARD_OK;
}

BoardStatus qspi_command_no_data(const QspiCommand* command) {
    BoardStatus status;

    status = qspi_validate_command(command);
    if (status != BOARD_OK) {
        return status;
    }

    if (command->data_mode != QSPI_IO_NONE) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    qspi_disable_transfer_control();
    qspi_clear_flags();

    QUADSPI->DLR = 0U;

    status = qspi_start_command(command, 0U);
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_flag(QUADSPI_SR_TCF);
    if (status != BOARD_OK) {
        return status;
    }

    qspi_clear_flags();

    return BOARD_OK;
}

BoardStatus qspi_read(const QspiCommand* command, void* buffer, size_t size) {
    BoardStatus status;

    status = qspi_validate_data_command(command, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

#if (QSPI_DMA_READ_ENABLED != 0)
    if (qspi_dma_auto_can_use(buffer, size) != 0U) {
        status = qspi_dma_start_transfer(command,
                                         buffer,
                                         size,
                                         QSPI_DMA_DIRECTION_READ);
        if (status == BOARD_OK) {
            return qspi_dma_wait_done();
        }

        if (status != BOARD_ERR_INVALID_ARG) {
            return status;
        }
    }
#endif

    return qspi_read_cpu(command, buffer, size);
}

BoardStatus qspi_write(const QspiCommand* command,
                       const void* buffer,
                       size_t size) {
    BoardStatus status;

    status = qspi_validate_data_command(command, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

#if (QSPI_DMA_WRITE_ENABLED != 0)
    if (qspi_dma_auto_can_use(buffer, size) != 0U) {
        status = qspi_dma_start_transfer(command,
                                         (void*)buffer,
                                         size,
                                         QSPI_DMA_DIRECTION_WRITE);
        if (status == BOARD_OK) {
            return qspi_dma_wait_done();
        }

        if (status != BOARD_ERR_INVALID_ARG) {
            return status;
        }
    }
#endif

    return qspi_write_cpu(command, buffer, size);
}

BoardStatus qspi_write_dma_start(const QspiCommand* command,
                                 const void* buffer,
                                 size_t size) {
    return qspi_dma_start_transfer(command,
                                   (void*)buffer,
                                   size,
                                   QSPI_DMA_DIRECTION_WRITE);
}

BoardStatus qspi_write_dma_poll(uint8_t* is_done) {
    return qspi_dma_poll_internal(is_done);
}

BoardStatus qspi_write_dma(const QspiCommand* command,
                           const void* buffer,
                           size_t size) {
    BoardStatus status;

    status = qspi_write_dma_start(command, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    return qspi_dma_wait_done();
}

BoardStatus qspi_auto_poll_start(const QspiCommand* command,
                                 uint8_t match_value,
                                 uint8_t match_mask) {
    BoardStatus status;

    status = qspi_validate_command(command);
    if (status != BOARD_OK) {
        return status;
    }

    if (command->data_mode == QSPI_IO_NONE) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = qspi_check_software_idle();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_wait_not_busy();
    if (status != BOARD_OK) {
        return status;
    }

    qspi_disable_transfer_control();
    qspi_clear_flags();

    QUADSPI->DLR = 0U;
    QUADSPI->PSMAR = match_value;
    QUADSPI->PSMKR = match_mask;
    QUADSPI->PIR = 0x10U;

    qspi_poll_state = QSPI_ASYNC_BUSY;
    qspi_poll_status = BOARD_OK;

    QUADSPI->CR |= QUADSPI_CR_APMS |
        QUADSPI_CR_SMIE |
        QUADSPI_CR_TEIE;

    status = qspi_start_command(command, 2U);
    if (status != BOARD_OK) {
        QUADSPI->CR &= ~(QUADSPI_CR_APMS |
            QUADSPI_CR_SMIE |
            QUADSPI_CR_TEIE);
        qspi_poll_state = QSPI_ASYNC_IDLE;
        qspi_poll_status = status;
        return status;
    }

    return BOARD_OK;
}

BoardStatus qspi_auto_poll_poll(uint8_t* is_done) {
    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    qspi_poll_process_flags();

    if (qspi_poll_state == QSPI_ASYNC_BUSY) {
        *is_done = 0U;
        return BOARD_OK;
    }

    if (qspi_poll_state == QSPI_ASYNC_DONE) {
        *is_done = 1U;
        qspi_poll_state = QSPI_ASYNC_IDLE;
        return qspi_poll_status;
    }

    *is_done = 1U;

    return qspi_poll_status;
}

BoardStatus qspi_auto_poll(const QspiCommand* command,
                           uint8_t match_value,
                           uint8_t match_mask) {
    BoardStatus status;
    uint8_t is_done = 0U;
    uint32_t timeout = QSPI_TIMEOUT_LOOPS;

    status = qspi_auto_poll_start(command, match_value, match_mask);
    if (status != BOARD_OK) {
        return status;
    }

    while (is_done == 0U) {
        if (timeout == 0U) {
            QUADSPI->CR &= ~(QUADSPI_CR_APMS |
                QUADSPI_CR_SMIE |
                QUADSPI_CR_TEIE);
            QUADSPI->CR |= QUADSPI_CR_ABORT;
            (void)qspi_wait_not_busy();

            qspi_clear_flags();

            qspi_poll_state = QSPI_ASYNC_IDLE;
            qspi_poll_status = BOARD_ERR_TIMEOUT;

            return BOARD_ERR_TIMEOUT;
        }

        status = qspi_auto_poll_poll(&is_done);
        if (status != BOARD_OK) {
            return status;
        }

        --timeout;
    }

    return BOARD_OK;
}

BoardStatus qspi_is_busy(uint8_t* is_busy) {
    if (is_busy == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_busy = ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus qspi_debug_snapshot(QspiDebugSnapshot* snapshot) {
    if (snapshot == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    qspi_debug_fill_snapshot(snapshot);

    return BOARD_OK;
}

BoardStatus qspi_debug_last_dma_timeout(QspiDebugSnapshot* snapshot, uint8_t* is_valid) {
    if ((snapshot == 0) || (is_valid == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *snapshot = qspi_dma_timeout_snapshot;
    *is_valid = qspi_dma_timeout_snapshot_valid;

    return BOARD_OK;
}

void DMA1_Channel5_IRQHandler(void) {
    qspi_dma_process_dma_flags();
}

void DMA1_CH5_IRQHandler(void) {
    qspi_dma_process_dma_flags();
}

void QUADSPI_IRQHandler(void) {
    qspi_dma_process_qspi_flags();
    qspi_poll_process_flags();
}
