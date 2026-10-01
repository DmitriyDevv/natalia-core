#include "nand_map.h"

#include "mram_store.h"

#define NAND_MAP_SCAN_POLL_LIMIT 4096U

static BoardNandBlockMap nand_map_buffer;

static BoardStatus nand_map_scan(uint8_t bank_id) {
    uint32_t polls;
    uint8_t is_done = 0U;
    BoardStatus status;

    status = board_nand_bad_block_scan_start(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    for (polls = 0U; (polls < NAND_MAP_SCAN_POLL_LIMIT) && (is_done == 0U); ++polls) {
        status = board_nand_bad_block_scan_poll(bank_id, &is_done);
        if (status != BOARD_OK) {
            return status;
        }
    }

    return (is_done != 0U) ? BOARD_OK : BOARD_ERR_TIMEOUT;
}

BoardStatus nand_map_save(uint8_t bank_id) {
    BoardStatus status;

    status = board_nand_get_block_map(bank_id, &nand_map_buffer);
    if (status != BOARD_OK) {
        return status;
    }

    return mram_store_save_block_map(bank_id, &nand_map_buffer);
}

BoardStatus nand_map_rebuild(uint8_t bank_id) {
    BoardStatus status;

    status = nand_map_scan(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_map_save(bank_id);
}

BoardStatus nand_map_load(uint8_t bank_id) {
    uint8_t is_valid = 0U;
    BoardStatus status;

    status = mram_store_load_block_map(bank_id, &nand_map_buffer, &is_valid);
    if (status != BOARD_OK) {
        return status;
    }

    if (is_valid == 0U) {
        return nand_map_rebuild(bank_id);
    }

    return board_nand_set_block_map(bank_id, &nand_map_buffer);
}
