#ifndef NATALIA_QSPI_H
#define NATALIA_QSPI_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    QSPI_BANK_1 = 1,
    QSPI_BANK_2 = 2
} QspiBank;

typedef enum {
    QSPI_IO_NONE = 0,
    QSPI_IO_1_LINE = 1,
    QSPI_IO_2_LINES = 2,
    QSPI_IO_4_LINES = 3
} QspiIoMode;

typedef enum {
    QSPI_ADDRESS_8_BIT = 0,
    QSPI_ADDRESS_16_BIT = 1,
    QSPI_ADDRESS_24_BIT = 2,
    QSPI_ADDRESS_32_BIT = 3
} QspiAddressSize;

typedef enum {
    QSPI_ALTERNATE_8_BIT = 0,
    QSPI_ALTERNATE_16_BIT = 1,
    QSPI_ALTERNATE_24_BIT = 2,
    QSPI_ALTERNATE_32_BIT = 3
} QspiAlternateSize;

typedef struct {
    uint8_t instruction;
    QspiIoMode instruction_mode;

    uint32_t address;
    QspiIoMode address_mode;
    QspiAddressSize address_size;

    uint32_t alternate;
    QspiIoMode alternate_mode;
    QspiAlternateSize alternate_size;

    uint8_t dummy_cycles;
    QspiIoMode data_mode;
} QspiCommand;

typedef struct {
    uint32_t qspi_cr;
    uint32_t qspi_dcr;
    uint32_t qspi_sr;
    uint32_t qspi_fcr;
    uint32_t qspi_dlr;
    uint32_t qspi_ccr;
    uint32_t qspi_ar;
    uint32_t qspi_abr;

    uint32_t dma_isr;
    uint32_t dma_ccr;
    uint32_t dma_cndtr;
    uint32_t dma_cpar;
    uint32_t dma_cmar;
    uint32_t dma_cselr;

    uint32_t dma_state;
    uint32_t dma_status;
    uint32_t poll_state;
    uint32_t poll_status;
} QspiDebugSnapshot;

BoardStatus qspi_init(void);

BoardStatus qspi_dma_init(void);

BoardStatus qspi_select_bank(QspiBank bank);

BoardStatus qspi_command_no_data(const QspiCommand* command);

BoardStatus qspi_read(const QspiCommand* command,
                      void* buffer,
                      size_t size);

BoardStatus qspi_write(const QspiCommand* command,
                       const void* buffer,
                       size_t size);

BoardStatus qspi_write_dma(const QspiCommand* command,
                           const void* buffer,
                           size_t size);

BoardStatus qspi_write_dma_start(const QspiCommand* command,
                                 const void* buffer,
                                 size_t size);

BoardStatus qspi_write_dma_poll(uint8_t* is_done);

BoardStatus qspi_auto_poll(const QspiCommand* command,
                           uint8_t match_value,
                           uint8_t match_mask);

BoardStatus qspi_auto_poll_start(const QspiCommand* command,
                                 uint8_t match_value,
                                 uint8_t match_mask);

BoardStatus qspi_auto_poll_poll(uint8_t* is_done);

BoardStatus qspi_is_busy(uint8_t* is_busy);

BoardStatus qspi_debug_snapshot(QspiDebugSnapshot* snapshot);

#endif