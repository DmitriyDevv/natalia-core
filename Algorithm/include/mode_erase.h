#ifndef NATALIA_MODE_ERASE_H
#define NATALIA_MODE_ERASE_H

#include <stdint.h>

#include "nand_mt29f.h"
#include "nand_storage.h"
#include "status.h"

typedef enum {
    MODE_ERASE_STATE_IDLE = 0,
    MODE_ERASE_STATE_START = 1,
    MODE_ERASE_STATE_WAIT = 2,
    MODE_ERASE_STATE_DONE = 3,
    MODE_ERASE_STATE_ERROR = 4
} ModeEraseState;

typedef struct {
    ModeEraseState state;
    NandMt29fBank bank;
    uint8_t bank_id;
    uint8_t keep_power_after;
    BoardStatus last_status;
    uint32_t first_block;
    uint32_t block_count;
    uint32_t erased_blocks;
    uint32_t skipped_bad_blocks;
    uint32_t failed_blocks;
    uint32_t next_block;
    uint8_t done;
    uint8_t error;
} ModeEraseInfo;

BoardStatus mode_erase_init(void);

BoardStatus mode_erase_start_bank(NandMt29fBank bank,
                                  uint8_t keep_power_after);

BoardStatus mode_erase_start_range(NandMt29fBank bank,
                                   uint8_t keep_power_after,
                                   uint32_t first_block,
                                   uint32_t block_count);

BoardStatus mode_erase_poll(uint8_t* is_done);

BoardStatus mode_erase_get_info(ModeEraseInfo* info);

#endif