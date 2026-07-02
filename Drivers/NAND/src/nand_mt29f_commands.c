#include "nand_mt29f_private.h"

QspiCommand nand_mt29f_make_command(uint8_t instruction) {
    QspiCommand command;

    command.instruction = instruction;
    command.instruction_mode = QSPI_IO_1_LINE;

    command.address = 0U;
    command.address_mode = QSPI_IO_NONE;
    command.address_size = QSPI_ADDRESS_8_BIT;

    command.alternate = 0U;
    command.alternate_mode = QSPI_IO_NONE;
    command.alternate_size = QSPI_ALTERNATE_8_BIT;

    command.dummy_cycles = 0U;
    command.data_mode = QSPI_IO_NONE;

    return command;
}

static QspiCommand nand_mt29f_make_feature_read_command(uint8_t reg) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_GET_FEATURE);
    command.address = reg;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_8_BIT;
    command.data_mode = QSPI_IO_1_LINE;

    return command;
}

static QspiCommand nand_mt29f_make_feature_write_command(uint8_t reg) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_SET_FEATURE);
    command.address = reg;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_8_BIT;
    command.data_mode = QSPI_IO_1_LINE;

    return command;
}

QspiCommand nand_mt29f_make_page_read_command(uint32_t row_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_PAGE_READ);
    command.address = row_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_24_BIT;

    return command;
}

QspiCommand nand_mt29f_make_read_cache_x1_command(uint32_t column_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_READ_FROM_CACHE_X1);
    command.address = column_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_16_BIT;
    command.dummy_cycles = 8U;
    command.data_mode = QSPI_IO_1_LINE;

    return command;
}

QspiCommand nand_mt29f_make_read_cache_x2_command(uint32_t column_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_READ_FROM_CACHE_X2);
    command.address = column_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_16_BIT;
    command.dummy_cycles = 8U;
    command.data_mode = QSPI_IO_2_LINES;

    return command;
}

QspiCommand nand_mt29f_make_read_cache_x4_command(uint32_t column_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_READ_FROM_CACHE_X4);
    command.address = column_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_16_BIT;
    command.dummy_cycles = 8U;
    command.data_mode = QSPI_IO_4_LINES;

    return command;
}

QspiCommand nand_mt29f_make_read_cache_command(uint32_t column_address) {
#if (NAND_MT29F_CACHE_READ_MODE == 1)
    return nand_mt29f_make_read_cache_x1_command(column_address);
#elif (NAND_MT29F_CACHE_READ_MODE == 2)
    return nand_mt29f_make_read_cache_x2_command(column_address);
#else
    return nand_mt29f_make_read_cache_x4_command(column_address);
#endif
}

QspiCommand nand_mt29f_make_program_load_command(uint32_t column_address) {
    QspiCommand command;

#if (NAND_MT29F_PROGRAM_LOAD_MODE == 4)
    command = nand_mt29f_make_command(NAND_CMD_PROGRAM_LOAD_X4);
    command.data_mode = QSPI_IO_4_LINES;
#else
    command = nand_mt29f_make_command(NAND_CMD_PROGRAM_LOAD_X1);
    command.data_mode = QSPI_IO_1_LINE;
#endif

    command.address = column_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_16_BIT;

    return command;
}

QspiCommand nand_mt29f_make_program_execute_command(uint32_t row_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_PROGRAM_EXECUTE);
    command.address = row_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_24_BIT;

    return command;
}

QspiCommand nand_mt29f_make_block_erase_command(uint32_t row_address) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_BLOCK_ERASE);
    command.address = row_address;
    command.address_mode = QSPI_IO_1_LINE;
    command.address_size = QSPI_ADDRESS_24_BIT;

    return command;
}

BoardStatus nand_mt29f_write_enable(void) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_WRITE_ENABLE);

    return qspi_command_no_data(&command);
}

BoardStatus nand_mt29f_reset(void) {
    QspiCommand command;

    command = nand_mt29f_make_command(NAND_CMD_RESET);

    return qspi_command_no_data(&command);
}

BoardStatus nand_mt29f_get_feature(uint8_t reg, uint8_t* value) {
    QspiCommand command;

    if (value == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    command = nand_mt29f_make_feature_read_command(reg);

    return qspi_read(&command, value, 1U);
}

BoardStatus nand_mt29f_set_feature(uint8_t reg, uint8_t value) {
    QspiCommand command;
    BoardStatus status;

    status = nand_mt29f_write_enable();
    if (status != BOARD_OK) {
        return status;
    }

    command = nand_mt29f_make_feature_write_command(reg);

    return qspi_write(&command, &value, 1U);
}

BoardStatus nand_mt29f_read_status(uint8_t* status_value) {
    return nand_mt29f_get_feature(NAND_REG_STATUS, status_value);
}

BoardStatus nand_mt29f_poll_ready_once(uint8_t* is_ready,
                                       uint8_t* status_value) {
    uint8_t status;
    BoardStatus result;

    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    result = nand_mt29f_read_status(&status);
    if (result != BOARD_OK) {
        return result;
    }

    if (status_value != 0) {
        *status_value = status;
    }

    *is_ready = ((status & NAND_STATUS_OIP) == 0U) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus nand_mt29f_wait_ready(uint8_t* status_value) {
    uint8_t is_ready = 0U;
    uint8_t status = 0U;
    uint32_t timeout = NAND_TIMEOUT_LOOPS;
    BoardStatus result;

    while (timeout > 0U) {
        result = nand_mt29f_poll_ready_once(&is_ready, &status);
        if (result != BOARD_OK) {
            return result;
        }

        if (is_ready != 0U) {
            if (status_value != 0) {
                *status_value = status;
            }

            return BOARD_OK;
        }

        --timeout;
    }

    return BOARD_ERR_TIMEOUT;
}
