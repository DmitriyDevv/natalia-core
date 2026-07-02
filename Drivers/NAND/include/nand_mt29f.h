#ifndef NATALIA_NAND_MT29F_H
#define NATALIA_NAND_MT29F_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

#define NAND_MT29F_BLOCKS_PER_LUN 2048UL
#define NAND_MT29F_PAGES_PER_BLOCK 64UL
#define NAND_MT29F_PAGE_SIZE 4096UL
#define NAND_MT29F_SPARE_SIZE 256UL
#define NAND_MT29F_PAGE_TOTAL_SIZE (NAND_MT29F_PAGE_SIZE + NAND_MT29F_SPARE_SIZE)
#define NAND_MT29F_BLOCK_SIZE (NAND_MT29F_PAGE_SIZE * NAND_MT29F_PAGES_PER_BLOCK)
#define NAND_MT29F_BLOCK_TOTAL_SIZE (NAND_MT29F_PAGE_TOTAL_SIZE * NAND_MT29F_PAGES_PER_BLOCK)

typedef enum {
    NAND_MT29F_BANK_1 = 1,
    NAND_MT29F_BANK_2 = 2
} NandMt29fBank;

typedef struct {
    uint8_t manufacturer_id;
    uint8_t device_id;
    uint8_t bytes[4];
} NandMt29fId;

typedef enum {
    NAND_MT29F_DEBUG_IO_NONE = 0,
    NAND_MT29F_DEBUG_IO_1_LINE = 1,
    NAND_MT29F_DEBUG_IO_2_LINES = 2,
    NAND_MT29F_DEBUG_IO_4_LINES = 4
} NandMt29fDebugIoMode;

BoardStatus nand_mt29f_select_bank(NandMt29fBank bank);

BoardStatus nand_mt29f_init(void);

BoardStatus nand_mt29f_read_id(NandMt29fId* id);

BoardStatus nand_mt29f_read_page(uint32_t block,
                                 uint32_t page,
                                 void* buffer,
                                 size_t size);

BoardStatus nand_mt29f_program_page(uint32_t block,
                                    uint32_t page,
                                    const void* buffer,
                                    size_t size);

BoardStatus nand_mt29f_program_page_dma_start(uint32_t block,
                                              uint32_t page,
                                              const void* buffer,
                                              size_t size);

BoardStatus nand_mt29f_program_page_dma_poll(uint8_t* is_done);

BoardStatus nand_mt29f_erase_block(uint32_t block);

BoardStatus nand_mt29f_is_block_bad(uint32_t block,
                                    uint8_t* is_bad);

BoardStatus nand_mt29f_set_ecc(uint8_t enabled);

uint8_t nand_mt29f_get_cache_read_mode(void);

uint8_t nand_mt29f_get_program_load_mode(void);

BoardStatus nand_mt29f_debug_read_cache_modes(uint32_t block,
                                              uint32_t page,
                                              void* buffer_x1,
                                              void* buffer_x2,
                                              void* buffer_x4,
                                              size_t size);

BoardStatus nand_mt29f_debug_read_cache_custom(uint32_t block,
                                               uint32_t page,
                                               uint8_t instruction,
                                               NandMt29fDebugIoMode address_mode,
                                               NandMt29fDebugIoMode alternate_mode,
                                               uint8_t dummy_cycles,
                                               void* buffer,
                                               size_t size);

#endif