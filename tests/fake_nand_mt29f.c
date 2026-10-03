#include "fake_nand_mt29f.h"

#include <string.h>

#define FAKE_HALVES_PER_PAGE (NAND_MT29F_PAGE_SIZE / FAKE_NAND_HALF_BYTES)
#define FAKE_HALVES_PER_BLOCK (NAND_MT29F_PAGES_PER_BLOCK * FAKE_HALVES_PER_PAGE)
#define FAKE_HALF_COUNT (NAND_MT29F_BLOCKS_PER_LUN * FAKE_HALVES_PER_BLOCK)

typedef struct {
    uint32_t token;
    uint8_t written;
    uint8_t overwritten;
} FakeHalf;

typedef struct {
    FakeHalf halves[FAKE_HALF_COUNT];
    uint8_t factory_bad[NAND_MT29F_BLOCKS_PER_LUN];
    uint8_t erase_ignored[NAND_MT29F_BLOCKS_PER_LUN];
    uint8_t erase_silent[NAND_MT29F_BLOCKS_PER_LUN];
    uint8_t read_uncorrectable[NAND_MT29F_BLOCKS_PER_LUN];
    uint8_t erase_fails[NAND_MT29F_BLOCKS_PER_LUN];
    uint8_t program_fails[NAND_MT29F_BLOCKS_PER_LUN];
} FakeBank;

#define FAKE_STATUS_ERASE_FAIL 0x04U
#define FAKE_STATUS_PROGRAM_FAIL 0x08U

static NandMt29fFault fake_last_fault;
static uint8_t fake_last_fault_status;

static FakeBank fake_banks[2];
static uint8_t fake_selected_bank;
static uint32_t fake_program_block;
static uint32_t fake_program_half;
static uint32_t fake_program_token;
static uint8_t fake_program_busy;
static uint32_t fake_program_polls_left;
static uint32_t fake_program_latency = 3U;

static FakeNandStats fake_stats;

static FakeBank* fake_bank(void) {
    return &fake_banks[fake_selected_bank];
}

static uint32_t fake_half_index(uint32_t block, uint32_t page, uint32_t column) {
    return (block * FAKE_HALVES_PER_BLOCK) +
        (page * FAKE_HALVES_PER_PAGE) +
        (column / FAKE_NAND_HALF_BYTES);
}

static uint32_t fake_token_of(const void* buffer) {
    uint32_t token = 0U;

    (void)memcpy(&token, buffer, sizeof(token));

    return token;
}

static void fake_render_half(const FakeHalf* half, uint8_t* buffer, size_t size) {
    uint32_t token;
    size_t offset;

    if (half->written == 0U) {
        (void)memset(buffer, 0xFF, size);
        return;
    }

    token = half->token;
    if (half->overwritten != 0U) {
        token ^= 0xA5A5A5A5U;
    }

    for (offset = 0U; offset < size; offset += sizeof(token)) {
        (void)memcpy(&buffer[offset], &token, sizeof(token));
    }
}

void fake_nand_set_program_latency(uint32_t polls) {
    fake_program_latency = polls;
}

void fake_nand_reset(void) {
    (void)memset(fake_banks, 0, sizeof(fake_banks));
    (void)memset(&fake_stats, 0, sizeof(fake_stats));
    fake_selected_bank = 0U;
    fake_program_busy = 0U;
    fake_program_polls_left = 0U;
    fake_program_block = 0U;
    fake_program_half = 0U;
    fake_program_token = 0U;
    fake_last_fault = NAND_MT29F_FAULT_NONE;
    fake_last_fault_status = 0U;
}

static BoardStatus fake_fault(NandMt29fFault fault, uint8_t chip_status, BoardStatus status) {
    fake_last_fault = fault;
    fake_last_fault_status = chip_status;

    return status;
}

NandMt29fFault nand_mt29f_get_last_fault(void) {
    return fake_last_fault;
}

uint8_t nand_mt29f_get_last_fault_status(void) {
    return fake_last_fault_status;
}

uint8_t nand_mt29f_last_program_unconfirmed(void) {
    return 0U;
}

void fake_nand_set_erase_fail(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].erase_fails[block] = 1U;
}

void fake_nand_set_factory_bad(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].factory_bad[block] = 1U;
}

void fake_nand_set_erase_ignored(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].erase_ignored[block] = 1U;
}

void fake_nand_set_erase_silent(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].erase_silent[block] = 1U;
}

void fake_nand_set_read_uncorrectable(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].read_uncorrectable[block] = 1U;
}

void fake_nand_set_program_fail(uint8_t bank_id, uint32_t block) {
    fake_banks[bank_id - 1U].program_fails[block] = 1U;
}

uint8_t fake_nand_block_is_erased(uint8_t bank_id, uint32_t block) {
    const FakeBank* bank = &fake_banks[bank_id - 1U];
    uint32_t half;

    for (half = 0U; half < FAKE_HALVES_PER_BLOCK; ++half) {
        if (bank->halves[(block * FAKE_HALVES_PER_BLOCK) + half].written != 0U) {
            return 0U;
        }
    }

    return 1U;
}

uint32_t fake_nand_half_token(uint8_t bank_id, uint32_t block, uint32_t page, uint32_t column) {
    const FakeHalf* half = &fake_banks[bank_id - 1U].halves[fake_half_index(block, page, column)];

    return (half->written != 0U) ? half->token : 0xFFFFFFFFU;
}

uint8_t fake_nand_half_overwritten(uint8_t bank_id, uint32_t block, uint32_t page, uint32_t column) {
    return fake_banks[bank_id - 1U].halves[fake_half_index(block, page, column)].overwritten;
}

FakeNandStats fake_nand_stats(void) {
    return fake_stats;
}

BoardStatus nand_mt29f_select_bank(NandMt29fBank bank) {
    if ((bank != NAND_MT29F_BANK_1) && (bank != NAND_MT29F_BANK_2)) {
        return BOARD_ERR_INVALID_ARG;
    }

    fake_selected_bank = (bank == NAND_MT29F_BANK_1) ? 0U : 1U;
    ++fake_stats.select_bank_calls;

    return BOARD_OK;
}

BoardStatus nand_mt29f_init(void) {
    ++fake_stats.init_calls;

    return BOARD_OK;
}

BoardStatus nand_mt29f_is_block_bad(uint32_t block, uint8_t* is_bad) {
    if ((is_bad == 0) || (block >= NAND_MT29F_BLOCKS_PER_LUN)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_bad = fake_bank()->factory_bad[block];
    ++fake_stats.is_block_bad_calls;

    return BOARD_OK;
}

BoardStatus nand_mt29f_erase_block(uint32_t block) {
    FakeBank* bank = fake_bank();
    uint32_t half;

    if (block >= NAND_MT29F_BLOCKS_PER_LUN) {
        return BOARD_ERR_INVALID_ARG;
    }

    ++fake_stats.erase_calls;
    fake_last_fault = NAND_MT29F_FAULT_NONE;
    fake_last_fault_status = 0U;

    if (bank->erase_ignored[block] != 0U) {
        return fake_fault(NAND_MT29F_FAULT_BUSY_NOT_SEEN, 0x00U, BOARD_ERR_IO);
    }

    if (bank->erase_fails[block] != 0U) {
        return fake_fault(NAND_MT29F_FAULT_ERASE_FAIL, FAKE_STATUS_ERASE_FAIL, BOARD_ERR_IO);
    }

    if (bank->erase_silent[block] != 0U) {
        return BOARD_OK;
    }

    for (half = 0U; half < FAKE_HALVES_PER_BLOCK; ++half) {
        bank->halves[(block * FAKE_HALVES_PER_BLOCK) + half].written = 0U;
        bank->halves[(block * FAKE_HALVES_PER_BLOCK) + half].overwritten = 0U;
        bank->halves[(block * FAKE_HALVES_PER_BLOCK) + half].token = 0U;
    }

    return BOARD_OK;
}

BoardStatus nand_mt29f_read_page_at(uint32_t block,
                                    uint32_t page,
                                    uint32_t column,
                                    void* buffer,
                                    size_t size) {
    if ((buffer == 0) || (block >= NAND_MT29F_BLOCKS_PER_LUN) ||
        (page >= NAND_MT29F_PAGES_PER_BLOCK) || (size > FAKE_NAND_HALF_BYTES) ||
        ((column % FAKE_NAND_HALF_BYTES) != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    ++fake_stats.read_calls;
    fake_render_half(&fake_bank()->halves[fake_half_index(block, page, column)], buffer, size);

    return (fake_bank()->read_uncorrectable[block] != 0U) ? BOARD_ERR_CRC : BOARD_OK;
}

BoardStatus nand_mt29f_program_page_dma_start_at(uint32_t block,
                                                 uint32_t page,
                                                 uint32_t column,
                                                 const void* buffer,
                                                 size_t size) {
    if ((buffer == 0) || (block >= NAND_MT29F_BLOCKS_PER_LUN) ||
        (page >= NAND_MT29F_PAGES_PER_BLOCK) || (size != FAKE_NAND_HALF_BYTES) ||
        ((column % FAKE_NAND_HALF_BYTES) != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (fake_program_busy != 0U) {
        return BOARD_ERR_BUSY;
    }

    fake_program_block = block;
    fake_program_half = fake_half_index(block, page, column);
    fake_program_token = fake_token_of(buffer);
    fake_program_busy = 1U;
    fake_program_polls_left = fake_program_latency;
    fake_last_fault = NAND_MT29F_FAULT_NONE;
    fake_last_fault_status = 0U;
    ++fake_stats.program_calls;

    return BOARD_OK;
}

BoardStatus nand_mt29f_program_page_dma_poll(uint8_t* is_done) {
    FakeBank* bank = fake_bank();
    FakeHalf* half;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (fake_program_busy == 0U) {
        *is_done = 1U;
        return BOARD_OK;
    }

    if (fake_program_polls_left > 0U) {
        --fake_program_polls_left;
        *is_done = 0U;
        return BOARD_OK;
    }

    fake_program_busy = 0U;
    *is_done = 1U;

    if (bank->program_fails[fake_program_block] != 0U) {
        return fake_fault(NAND_MT29F_FAULT_PROGRAM_FAIL, FAKE_STATUS_PROGRAM_FAIL, BOARD_ERR_IO);
    }

    half = &bank->halves[fake_program_half];
    if (half->written != 0U) {
        half->overwritten = 1U;
    }

    half->written = 1U;
    half->token = fake_program_token;

    return BOARD_OK;
}
