#include "mode_erase.h"

#include <string.h>

#include "../../BSP/Board_API/include/board_api.h"

typedef struct {
    ModeEraseState state;
    NandMt29fBank bank;
    uint8_t bank_id;
    uint8_t keep_power_after;
    BoardStatus last_status;
    uint32_t first_block;
    uint32_t block_count;
    uint8_t done;
    uint8_t error;
} ModeEraseContext;

static ModeEraseContext erase_context;

static uint8_t mode_erase_bank_id(NandMt29fBank bank) {
    if (bank == NAND_MT29F_BANK_1) {
        return 1U;
    }

    if (bank == NAND_MT29F_BANK_2) {
        return 2U;
    }

    return 0U;
}

static uint8_t mode_erase_bank_valid(NandMt29fBank bank) {
    if (bank == NAND_MT29F_BANK_1) {
        return 1U;
    }

    if (bank == NAND_MT29F_BANK_2) {
        return 1U;
    }

    return 0U;
}

static void mode_erase_set_error(BoardStatus status) {
    erase_context.state = MODE_ERASE_STATE_ERROR;
    erase_context.last_status = status;
    erase_context.error = 1U;
    erase_context.done = 1U;
}

BoardStatus mode_erase_init(void) {
    memset(&erase_context, 0, sizeof(erase_context));

    erase_context.state = MODE_ERASE_STATE_IDLE;
    erase_context.bank = NAND_MT29F_BANK_1;
    erase_context.bank_id = 1U;
    erase_context.keep_power_after = 1U;
    erase_context.last_status = BOARD_OK;

    return BOARD_OK;
}

BoardStatus mode_erase_start_range(NandMt29fBank bank,
                                   uint8_t keep_power_after,
                                   uint32_t first_block,
                                   uint32_t block_count) {
    BoardStatus status;
    uint8_t bank_id;

    if (mode_erase_bank_valid(bank) == 0U) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (first_block >= NAND_MT29F_BLOCKS_PER_LUN) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (block_count == 0U) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (block_count > (NAND_MT29F_BLOCKS_PER_LUN - first_block)) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if ((erase_context.state == MODE_ERASE_STATE_START) ||
        (erase_context.state == MODE_ERASE_STATE_WAIT)) {
        erase_context.last_status = BOARD_ERR_BUSY;
        return BOARD_ERR_BUSY;
    }

    bank_id = mode_erase_bank_id(bank);
    if (bank_id == 0U) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    memset(&erase_context, 0, sizeof(erase_context));

    erase_context.state = MODE_ERASE_STATE_START;
    erase_context.bank = bank;
    erase_context.bank_id = bank_id;
    erase_context.keep_power_after = keep_power_after;
    erase_context.first_block = first_block;
    erase_context.block_count = block_count;
    erase_context.last_status = BOARD_OK;

    status = board_nand_power_on(bank_id);
    if (status != BOARD_OK) {
        mode_erase_set_error(status);
        return status;
    }

    status = board_nand_connect(bank_id);
    if (status != BOARD_OK) {
        mode_erase_set_error(status);
        return status;
    }

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        mode_erase_set_error(status);
        return status;
    }

    status = nand_storage_erase_range_start(bank,
                                            first_block,
                                            block_count);
    if (status != BOARD_OK) {
        mode_erase_set_error(status);
        return status;
    }

    erase_context.state = MODE_ERASE_STATE_WAIT;
    erase_context.last_status = BOARD_OK;

    return BOARD_OK;
}

BoardStatus mode_erase_start_bank(NandMt29fBank bank,
                                  uint8_t keep_power_after) {
    return mode_erase_start_range(bank,
                                  keep_power_after,
                                  0U,
                                  NAND_MT29F_BLOCKS_PER_LUN);
}

BoardStatus mode_erase_poll(uint8_t* is_done) {
    BoardStatus status;
    uint8_t storage_done;

    if (is_done == 0) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    *is_done = 0U;

    if (erase_context.state == MODE_ERASE_STATE_IDLE) {
        erase_context.last_status = BOARD_OK;
        *is_done = 1U;
        return BOARD_OK;
    }

    if (erase_context.state == MODE_ERASE_STATE_DONE) {
        *is_done = 1U;
        return BOARD_OK;
    }

    if (erase_context.state == MODE_ERASE_STATE_ERROR) {
        *is_done = 1U;
        return erase_context.last_status;
    }

    if (erase_context.state != MODE_ERASE_STATE_WAIT) {
        erase_context.last_status = BOARD_ERR_BUSY;
        return BOARD_ERR_BUSY;
    }

    storage_done = 0U;

    status = nand_storage_erase_poll(&storage_done);
    if (status != BOARD_OK) {
        mode_erase_set_error(status);
        return status;
    }

    if (storage_done != 0U) {
        erase_context.state = MODE_ERASE_STATE_DONE;
        erase_context.done = 1U;
        erase_context.error = 0U;
        erase_context.last_status = BOARD_OK;
        *is_done = 1U;
        return BOARD_OK;
    }

    erase_context.last_status = BOARD_OK;

    return BOARD_OK;
}

BoardStatus mode_erase_get_info(ModeEraseInfo* info) {
    BoardStatus status;
    NandStorageInfo storage_info;

    if (info == 0) {
        mode_erase_set_error(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    memset(&storage_info, 0, sizeof(storage_info));

    status = nand_storage_get_info(&storage_info);
    if (status != BOARD_OK) {
        return status;
    }

    info->state = erase_context.state;
    info->bank = erase_context.bank;
    info->bank_id = erase_context.bank_id;
    info->keep_power_after = erase_context.keep_power_after;
    info->last_status = erase_context.last_status;
    info->first_block = erase_context.first_block;
    info->block_count = erase_context.block_count;
    info->erased_blocks = storage_info.erase_erased_blocks;
    info->skipped_bad_blocks = storage_info.erase_skipped_bad_blocks;
    info->failed_blocks = storage_info.erase_failed_blocks;
    info->next_block = storage_info.erase_next_block;
    info->done = erase_context.done;
    info->error = erase_context.error;

    return BOARD_OK;
}