#ifndef NATALIA_TESTS_FAKE_NAND_MT29F_H
#define NATALIA_TESTS_FAKE_NAND_MT29F_H

#include <stddef.h>
#include <stdint.h>

#include "nand_mt29f.h"
#include "status.h"

#define FAKE_NAND_HALF_BYTES 2048UL

typedef struct {
    uint32_t select_bank_calls;
    uint32_t init_calls;
    uint32_t erase_calls;
    uint32_t read_calls;
    uint32_t program_calls;
    uint32_t is_block_bad_calls;
} FakeNandStats;

void fake_nand_reset(void);

void fake_nand_set_program_latency(uint32_t polls);

void fake_nand_set_factory_bad(uint8_t bank_id, uint32_t block);

void fake_nand_set_erase_ignored(uint8_t bank_id, uint32_t block);

void fake_nand_set_erase_fail(uint8_t bank_id, uint32_t block);

void fake_nand_set_program_fail(uint8_t bank_id, uint32_t block);

uint8_t fake_nand_block_is_erased(uint8_t bank_id, uint32_t block);

uint32_t fake_nand_half_token(uint8_t bank_id, uint32_t block, uint32_t page, uint32_t column);

uint8_t fake_nand_half_overwritten(uint8_t bank_id, uint32_t block, uint32_t page, uint32_t column);

FakeNandStats fake_nand_stats(void);

#endif /* NATALIA_TESTS_FAKE_NAND_MT29F_H */
