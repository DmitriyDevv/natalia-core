#ifndef NATALIA_NAND_MT29F_PRIVATE_H
#define NATALIA_NAND_MT29F_PRIVATE_H

#include <stdint.h>

#include "nand_mt29f.h"
#include "qspi.h"
#include "status.h"

#define NAND_CMD_READ_ID 0x9FU
#define NAND_CMD_GET_FEATURE 0x0FU
#define NAND_CMD_SET_FEATURE 0x1FU
#define NAND_CMD_WRITE_ENABLE 0x06U
#define NAND_CMD_BLOCK_ERASE 0xD8U
#define NAND_CMD_PAGE_READ 0x13U
#define NAND_CMD_READ_FROM_CACHE_X1 0x03U
#define NAND_CMD_READ_FROM_CACHE_X2 0x3BU
#define NAND_CMD_READ_FROM_CACHE_X4 0x6BU
#define NAND_CMD_READ_FROM_CACHE_X4_IO 0xEBU
#define NAND_CMD_PROGRAM_LOAD_X1 0x02U
#define NAND_CMD_PROGRAM_LOAD_X4 0x32U
#define NAND_CMD_PROGRAM_EXECUTE 0x10U
#define NAND_CMD_RESET 0xFFU

#define NAND_REG_BLOCK_LOCK 0xA0U
#define NAND_REG_CONFIGURATION 0xB0U
#define NAND_REG_STATUS 0xC0U

#define NAND_STATUS_OIP 0x01U
#define NAND_STATUS_ERASE_FAIL 0x04U
#define NAND_STATUS_PROGRAM_FAIL 0x08U

#define NAND_CONFIGURATION_CONTINUOUS_READ 0x01U
#define NAND_CONFIGURATION_DRIVER_STRENGTH_MASK 0x0CU
#define NAND_CONFIGURATION_ECC_ENABLE 0x10U

#define NAND_EXPECTED_MANUFACTURER_ID 0x2CU
#define NAND_EXPECTED_DEVICE_ID 0x34U

#define NAND_TIMEOUT_LOOPS 8000000UL

#ifndef NAND_MT29F_CACHE_READ_MODE
#define NAND_MT29F_CACHE_READ_MODE 1
#endif

#ifndef NAND_MT29F_PROGRAM_LOAD_MODE
#define NAND_MT29F_PROGRAM_LOAD_MODE 4
#endif

#if (NAND_MT29F_CACHE_READ_MODE != 1) && \
    (NAND_MT29F_CACHE_READ_MODE != 2) && \
    (NAND_MT29F_CACHE_READ_MODE != 4)
#error "NAND_MT29F_CACHE_READ_MODE must be 1, 2 or 4"
#endif

#if (NAND_MT29F_PROGRAM_LOAD_MODE != 1) && \
    (NAND_MT29F_PROGRAM_LOAD_MODE != 4)
#error "NAND_MT29F_PROGRAM_LOAD_MODE must be 1 or 4"
#endif

QspiCommand nand_mt29f_make_command(uint8_t instruction);

QspiCommand nand_mt29f_make_page_read_command(uint32_t row_address);

QspiCommand nand_mt29f_make_read_cache_x1_command(uint32_t column_address);

QspiCommand nand_mt29f_make_read_cache_x2_command(uint32_t column_address);

QspiCommand nand_mt29f_make_read_cache_x4_command(uint32_t column_address);

QspiCommand nand_mt29f_make_read_cache_command(uint32_t column_address);

QspiCommand nand_mt29f_make_program_load_command(uint32_t column_address);

QspiCommand nand_mt29f_make_program_execute_command(uint32_t row_address);

QspiCommand nand_mt29f_make_block_erase_command(uint32_t row_address);

BoardStatus nand_mt29f_write_enable(void);

BoardStatus nand_mt29f_reset(void);

BoardStatus nand_mt29f_get_feature(uint8_t reg, uint8_t* value);

BoardStatus nand_mt29f_set_feature(uint8_t reg, uint8_t value);

BoardStatus nand_mt29f_read_status(uint8_t* status_value);

BoardStatus nand_mt29f_poll_ready_once(uint8_t* is_ready,
                                       uint8_t* status_value);

BoardStatus nand_mt29f_wait_ready(uint8_t* status_value);

#endif