#include "nand_mt29f.h"

#include <stdint.h>

#include "nand_mt29f_private.h"
#include "qspi.h"

typedef enum {
    NAND_PROGRAM_DMA_IDLE = 0,
    NAND_PROGRAM_DMA_LOAD,
    NAND_PROGRAM_DMA_WAIT_READY
} NandProgramDmaState;

static NandProgramDmaState nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
static uint8_t nand_program_dma_status_value = 0U;
static uint32_t nand_program_dma_row_address = 0U;
static uint32_t nand_program_dma_timeout = 0U;

static uint32_t make_row_address(uint32_t block, uint32_t page) {
    return (block * NAND_MT29F_PAGES_PER_BLOCK) + page;
}

static BoardStatus validate_block(uint32_t block) {
    if (block >= NAND_MT29F_BLOCKS_PER_LUN) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus validate_page_access(uint32_t block,
                                        uint32_t page,
                                        const void* buffer,
                                        size_t size) {
    BoardStatus status;

    status = validate_block(block);
    if (status != BOARD_OK) {
        return status;
    }

    if (page >= NAND_MT29F_PAGES_PER_BLOCK) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > NAND_MT29F_PAGE_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus validate_dma_page_access(uint32_t block,
                                            uint32_t page,
                                            const void* buffer,
                                            size_t size) {
    BoardStatus status;

    status = validate_page_access(block, page, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    if (((uintptr_t)buffer & 3UL) != 0UL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((size & 3UL) != 0UL) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

BoardStatus nand_mt29f_select_bank(NandMt29fBank bank) {
    if (bank == NAND_MT29F_BANK_1) {
        return qspi_select_bank(QSPI_BANK_1);
    }

    if (bank == NAND_MT29F_BANK_2) {
        return qspi_select_bank(QSPI_BANK_2);
    }

    return BOARD_ERR_INVALID_ARG;
}

uint8_t nand_mt29f_get_cache_read_mode(void) {
    return (uint8_t)NAND_MT29F_CACHE_READ_MODE;
}

uint8_t nand_mt29f_get_program_load_mode(void) {
    return (uint8_t)NAND_MT29F_PROGRAM_LOAD_MODE;
}

BoardStatus nand_mt29f_read_id(NandMt29fId* id) {
    QspiCommand command;
    uint8_t buffer[2] = {0U, 0U};
    BoardStatus status;

    if (id == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    command = nand_mt29f_make_command(NAND_CMD_READ_ID);
    command.dummy_cycles = 8U;
    command.data_mode = QSPI_IO_1_LINE;

    status = qspi_read(&command, buffer, sizeof(buffer));
    if (status != BOARD_OK) {
        return status;
    }

    id->manufacturer_id = buffer[0];
    id->device_id = buffer[1];
    id->bytes[0] = buffer[0];
    id->bytes[1] = buffer[1];
    id->bytes[2] = 0U;
    id->bytes[3] = 0U;

    if (id->manufacturer_id != NAND_EXPECTED_MANUFACTURER_ID) {
        return BOARD_ERR_IO;
    }

    if (id->device_id != NAND_EXPECTED_DEVICE_ID) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

BoardStatus nand_mt29f_set_ecc(uint8_t enabled) {
    uint8_t configuration;
    BoardStatus status;

    status = nand_mt29f_get_feature(NAND_REG_CONFIGURATION, &configuration);
    if (status != BOARD_OK) {
        return status;
    }

    if (enabled != 0U) {
        configuration |= NAND_CONFIGURATION_ECC_ENABLE;
    } else {
        configuration &= (uint8_t)~NAND_CONFIGURATION_ECC_ENABLE;
    }

    return nand_mt29f_set_feature(NAND_REG_CONFIGURATION, configuration);
}

BoardStatus nand_mt29f_init(void) {
    uint8_t configuration;
    BoardStatus status;

    status = nand_mt29f_reset();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_wait_ready(0);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_set_feature(NAND_REG_BLOCK_LOCK, 0x02U);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_get_feature(NAND_REG_CONFIGURATION, &configuration);
    if (status != BOARD_OK) {
        return status;
    }

    configuration &= (uint8_t)~NAND_CONFIGURATION_CONTINUOUS_READ;
    configuration &= (uint8_t)~NAND_CONFIGURATION_DRIVER_STRENGTH_MASK;
    configuration |= NAND_CONFIGURATION_ECC_ENABLE;

    return nand_mt29f_set_feature(NAND_REG_CONFIGURATION, configuration);
}

BoardStatus nand_mt29f_erase_block(uint32_t block) {
    QspiCommand command;
    uint8_t status_value = 0U;
    BoardStatus status;
    uint32_t row_address;

    status = validate_block(block);
    if (status != BOARD_OK) {
        return status;
    }

    row_address = block * NAND_MT29F_PAGES_PER_BLOCK;

    status = nand_mt29f_write_enable();
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_block_erase_command(row_address);

    status = qspi_command_no_data(&command);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_wait_ready(&status_value);
    if (status != BOARD_OK) {
        return status;
    }

    if ((status_value & NAND_STATUS_ERASE_FAIL) != 0U) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

BoardStatus nand_mt29f_program_page(uint32_t block,
                                    uint32_t page,
                                    const void* buffer,
                                    size_t size) {
    QspiCommand command;
    uint8_t status_value = 0U;
    BoardStatus status;
    uint32_t row_address;

    status = validate_page_access(block, page, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    row_address = make_row_address(block, page);

    status = nand_mt29f_write_enable();
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_program_load_command(0U);

    status = qspi_write(&command, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_program_execute_command(row_address);

    status = qspi_command_no_data(&command);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_wait_ready(&status_value);
    if (status != BOARD_OK) {
        return status;
    }

    if ((status_value & NAND_STATUS_PROGRAM_FAIL) != 0U) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

BoardStatus nand_mt29f_read_page(uint32_t block,
                                 uint32_t page,
                                 void* buffer,
                                 size_t size) {
    QspiCommand command;
    BoardStatus status;
    uint32_t row_address;

    status = validate_page_access(block, page, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    row_address = make_row_address(block, page);

    command = nand_mt29f_make_page_read_command(row_address);

    status = qspi_command_no_data(&command);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_wait_ready(0);
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_read_cache_command(0U);

    return qspi_read(&command, buffer, size);
}

BoardStatus nand_mt29f_is_block_bad(uint32_t block, uint8_t* is_bad) {
    QspiCommand command;
    uint8_t marker = 0U;
    BoardStatus status;
    uint32_t row_address;

    if (is_bad == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = validate_block(block);
    if (status != BOARD_OK) {
        return status;
    }

    row_address = block * NAND_MT29F_PAGES_PER_BLOCK;

    command = nand_mt29f_make_page_read_command(row_address);

    status = qspi_command_no_data(&command);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_wait_ready(0);
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_read_cache_x1_command(NAND_MT29F_PAGE_SIZE);

    status = qspi_read(&command, &marker, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    *is_bad = (marker != 0xFFU) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus nand_mt29f_program_page_dma_start(uint32_t block,
                                              uint32_t page,
                                              const void* buffer,
                                              size_t size) {
    QspiCommand command;
    BoardStatus status;

    if (nand_program_dma_state != NAND_PROGRAM_DMA_IDLE) {
        return BOARD_ERR_BUSY;
    }

    status = validate_dma_page_access(block, page, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    nand_program_dma_row_address = make_row_address(block, page);
    nand_program_dma_status_value = 0U;
    nand_program_dma_timeout = NAND_TIMEOUT_LOOPS;

    status = nand_mt29f_write_enable();
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_program_load_command(0U);

    status = qspi_write_dma_start(&command, buffer, size);
    if (status != BOARD_OK) {
        nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
        return status;
    }

    nand_program_dma_state = NAND_PROGRAM_DMA_LOAD;

    return BOARD_OK;
}

BoardStatus nand_mt29f_program_page_dma_poll(uint8_t* is_done) {
    QspiCommand command;
    BoardStatus status;
    uint8_t qspi_done = 0U;
    uint8_t nand_ready = 0U;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_done = 0U;

    if (nand_program_dma_state == NAND_PROGRAM_DMA_IDLE) {
        *is_done = 1U;
        return BOARD_OK;
    }

    if (nand_program_dma_state == NAND_PROGRAM_DMA_LOAD) {
        status = qspi_write_dma_poll(&qspi_done);
        if (status != BOARD_OK) {
            nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
            *is_done = 1U;
            return status;
        }

        if (qspi_done == 0U) {
            return BOARD_OK;
        }

        command = nand_mt29f_make_program_execute_command(nand_program_dma_row_address);

        status = qspi_command_no_data(&command);
        if (status != BOARD_OK) {
            nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
            *is_done = 1U;
            return status;
        }

        nand_program_dma_timeout = NAND_TIMEOUT_LOOPS;
        nand_program_dma_state = NAND_PROGRAM_DMA_WAIT_READY;

        return BOARD_OK;
    }

    if (nand_program_dma_state == NAND_PROGRAM_DMA_WAIT_READY) {
        if (nand_program_dma_timeout == 0U) {
            nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
            *is_done = 1U;
            return BOARD_ERR_TIMEOUT;
        }

        status = nand_mt29f_poll_ready_once(&nand_ready,
                                            &nand_program_dma_status_value);
        if (status != BOARD_OK) {
            nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
            *is_done = 1U;
            return status;
        }

        --nand_program_dma_timeout;

        if (nand_ready == 0U) {
            return BOARD_OK;
        }

        nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
        *is_done = 1U;

        if ((nand_program_dma_status_value & NAND_STATUS_PROGRAM_FAIL) != 0U) {
            return BOARD_ERR_IO;
        }

        return BOARD_OK;
    }

    nand_program_dma_state = NAND_PROGRAM_DMA_IDLE;
    *is_done = 1U;

    return BOARD_ERR_IO;
}

static BoardStatus nand_debug_load_page_to_cache(uint32_t block, uint32_t page) {
    QspiCommand command;
    BoardStatus status;
    uint32_t row_address;

    row_address = make_row_address(block, page);
    command = nand_mt29f_make_page_read_command(row_address);

    status = qspi_command_no_data(&command);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_mt29f_wait_ready(0);
}

static BoardStatus nand_debug_read_cache_raw(uint8_t instruction,
                                             QspiIoMode data_mode,
                                             void* buffer,
                                             size_t size) {
    QspiCommand command;

    command = nand_mt29f_make_command(instruction);
    command.address = 0U;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_16_BIT;
    command.dummy_cycles = 8U;
    command.data_mode = data_mode;

    return qspi_read(&command, buffer, size);
}

BoardStatus nand_mt29f_debug_read_cache_modes(uint32_t block,
                                              uint32_t page,
                                              void* buffer_x1,
                                              void* buffer_x2,
                                              void* buffer_x4,
                                              size_t size) {
    BoardStatus status;

    status = validate_page_access(block, page, buffer_x1, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = validate_page_access(block, page, buffer_x2, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = validate_page_access(block, page, buffer_x4, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_debug_load_page_to_cache(block, page);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_debug_read_cache_raw(NAND_CMD_READ_FROM_CACHE_X1,
                                       QSPI_IO_1_LINE,
                                       buffer_x1,
                                       size);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_debug_load_page_to_cache(block, page);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_debug_read_cache_raw(NAND_CMD_READ_FROM_CACHE_X2,
                                       QSPI_IO_2_LINES,
                                       buffer_x2,
                                       size);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_debug_load_page_to_cache(block, page);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_debug_read_cache_raw(NAND_CMD_READ_FROM_CACHE_X4,
                                     QSPI_IO_4_LINES,
                                     buffer_x4,
                                     size);
}


static QspiIoMode nand_debug_convert_io_mode(NandMt29fDebugIoMode mode) {
    if (mode == NAND_MT29F_DEBUG_IO_NONE) {
        return QSPI_IO_NONE;
    }

    if (mode == NAND_MT29F_DEBUG_IO_1_LINE) {
        return QSPI_IO_1_LINE;
    }

    if (mode == NAND_MT29F_DEBUG_IO_2_LINES) {
        return QSPI_IO_2_LINES;
    }

    if (mode == NAND_MT29F_DEBUG_IO_4_LINES) {
        return QSPI_IO_4_LINES;
    }

    return QSPI_IO_NONE;
}

BoardStatus nand_mt29f_debug_read_cache_custom(uint32_t block,
                                               uint32_t page,
                                               uint8_t instruction,
                                               NandMt29fDebugIoMode address_mode,
                                               NandMt29fDebugIoMode alternate_mode,
                                               uint8_t dummy_cycles,
                                               void* buffer,
                                               size_t size) {
    QspiCommand command;
    BoardStatus status;
    QspiIoMode qspi_address_mode;
    QspiIoMode qspi_alternate_mode;

    status = validate_page_access(block, page, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    qspi_address_mode = nand_debug_convert_io_mode(address_mode);
    qspi_alternate_mode = nand_debug_convert_io_mode(alternate_mode);

    if (address_mode != NAND_MT29F_DEBUG_IO_NONE) {
        if (qspi_address_mode == QSPI_IO_NONE) {
            return BOARD_ERR_INVALID_ARG;
        }
    }

    if (alternate_mode != NAND_MT29F_DEBUG_IO_NONE) {
        if (qspi_alternate_mode == QSPI_IO_NONE) {
            return BOARD_ERR_INVALID_ARG;
        }
    }

    status = nand_debug_load_page_to_cache(block, page);
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_command(instruction);
    command.address = 0U;
    command.address_mode = qspi_address_mode;
    command.address_size = QSPI_ADDRESS_16_BIT;
    command.alternate = 0U;
    command.alternate_mode = qspi_alternate_mode;
    command.alternate_size = QSPI_ALTERNATE_8_BIT;
    command.dummy_cycles = dummy_cycles;
    command.data_mode = QSPI_IO_4_LINES;

    return qspi_read(&command, buffer, size);
}