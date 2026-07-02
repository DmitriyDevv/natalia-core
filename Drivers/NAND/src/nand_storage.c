#include "nand_storage.h"

#include <stdint.h>
#include <string.h>

#define NAND_STORAGE_BAD_UNKNOWN 0U
#define NAND_STORAGE_BAD_GOOD 1U
#define NAND_STORAGE_BAD_BAD 2U

typedef struct {
    uint32_t block;
    uint32_t page;
} NandStorageLocation;

typedef struct {
    uint8_t valid;
    uint8_t buffer_index;
    uint32_t packet_index;
    NandStorageLocation location;
} NandStorageWriteRequest;

static uint8_t storage_page_buffers[2U][NAND_MT29F_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t storage_read_page_buffer[NAND_MT29F_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t storage_bad_block_cache[NAND_MT29F_BLOCKS_PER_LUN];

static NandStorageMode storage_mode = NAND_STORAGE_MODE_IDLE;
static NandMt29fBank storage_bank = NAND_MT29F_BANK_1;
static uint8_t storage_mounted = 0U;
static uint8_t storage_is_full = 0U;
static BoardStatus storage_last_status = BOARD_OK;

static uint32_t storage_committed_packet_count = 0U;
static uint32_t storage_next_packet_index = 0U;
static uint32_t storage_next_block = 0U;
static uint32_t storage_next_page = 0U;

static uint32_t storage_read_packet_count = 0U;
static uint32_t storage_read_next_packet_index = 0U;

static uint32_t storage_erase_start_block = 0U;
static uint32_t storage_erase_next_block = 0U;
static uint32_t storage_erase_end_block = 0U;
static uint32_t storage_erase_erased_blocks = 0U;
static uint32_t storage_erase_skipped_bad_blocks = 0U;
static uint32_t storage_erase_failed_blocks = 0U;

static NandStorageWriteRequest storage_active_write;
static NandStorageWriteRequest storage_queued_write;

#if (NAND_STORAGE_PACKET_SIZE > NAND_MT29F_PAGE_SIZE)
#error "NAND_STORAGE_PACKET_SIZE must not exceed NAND_MT29F_PAGE_SIZE"
#endif

static void storage_set_status(BoardStatus status) {
    storage_last_status = status;

    if (status != BOARD_OK) {
        if (status != BOARD_ERR_BUSY) {
            storage_mode = NAND_STORAGE_MODE_ERROR;
        }
    }
}

static uint8_t storage_bank_is_valid(NandMt29fBank bank) {
    if (bank == NAND_MT29F_BANK_1) {
        return 1U;
    }

    if (bank == NAND_MT29F_BANK_2) {
        return 1U;
    }

    return 0U;
}

static void storage_clear_bad_block_cache(void) {
    uint32_t index;

    for (index = 0U; index < NAND_MT29F_BLOCKS_PER_LUN; ++index) {
        storage_bad_block_cache[index] = NAND_STORAGE_BAD_UNKNOWN;
    }
}

static void storage_clear_write_requests(void) {
    storage_active_write.valid = 0U;
    storage_active_write.buffer_index = 0U;
    storage_active_write.packet_index = 0U;
    storage_active_write.location.block = 0U;
    storage_active_write.location.page = 0U;

    storage_queued_write.valid = 0U;
    storage_queued_write.buffer_index = 0U;
    storage_queued_write.packet_index = 0U;
    storage_queued_write.location.block = 0U;
    storage_queued_write.location.page = 0U;
}

static void storage_reset_runtime_state(void) {
    storage_mode = NAND_STORAGE_MODE_IDLE;
    storage_is_full = 0U;
    storage_last_status = BOARD_OK;

    storage_committed_packet_count = 0U;
    storage_next_packet_index = 0U;
    storage_next_block = 0U;
    storage_next_page = 0U;

    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;

    storage_erase_start_block = 0U;
    storage_erase_next_block = 0U;
    storage_erase_end_block = 0U;
    storage_erase_erased_blocks = 0U;
    storage_erase_skipped_bad_blocks = 0U;
    storage_erase_failed_blocks = 0U;

    storage_clear_write_requests();
}

static BoardStatus storage_select_mounted_bank(void) {
    BoardStatus status;

    if (storage_mounted == 0U) {
        storage_set_status(BOARD_ERR_IO);
        return BOARD_ERR_IO;
    }

    status = nand_mt29f_select_bank(storage_bank);
    storage_set_status(status);

    return status;
}

static BoardStatus storage_get_block_bad_state(uint32_t block,
                                               uint8_t* is_bad) {
    BoardStatus status;
    uint8_t bad;

    if (is_bad == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (block >= NAND_MT29F_BLOCKS_PER_LUN) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (storage_bad_block_cache[block] == NAND_STORAGE_BAD_GOOD) {
        *is_bad = 0U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if (storage_bad_block_cache[block] == NAND_STORAGE_BAD_BAD) {
        *is_bad = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = nand_mt29f_is_block_bad(block, &bad);
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    if (bad != 0U) {
        storage_bad_block_cache[block] = NAND_STORAGE_BAD_BAD;
        *is_bad = 1U;
    } else {
        storage_bad_block_cache[block] = NAND_STORAGE_BAD_GOOD;
        *is_bad = 0U;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

static void storage_mark_block_bad(uint32_t block) {
    if (block < NAND_MT29F_BLOCKS_PER_LUN) {
        storage_bad_block_cache[block] = NAND_STORAGE_BAD_BAD;
    }
}

static BoardStatus storage_find_packet_location(uint32_t packet_index,
                                                NandStorageLocation* location) {
    BoardStatus status;
    uint32_t block;
    uint32_t logical_base;
    uint8_t is_bad;

    if (location == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    logical_base = 0U;

    for (block = 0U; block < NAND_MT29F_BLOCKS_PER_LUN; ++block) {
        status = storage_get_block_bad_state(block, &is_bad);
        if (status != BOARD_OK) {
            return status;
        }

        if (is_bad != 0U) {
            continue;
        }

        if (packet_index < (logical_base + NAND_MT29F_PAGES_PER_BLOCK)) {
            location->block = block;
            location->page = packet_index - logical_base;
            storage_set_status(BOARD_OK);
            return BOARD_OK;
        }

        logical_base += NAND_MT29F_PAGES_PER_BLOCK;
    }

    storage_set_status(BOARD_ERR_IO);
    return BOARD_ERR_IO;
}

static BoardStatus storage_update_next_location(uint32_t packet_index) {
    BoardStatus status;
    NandStorageLocation location;

    status = storage_find_packet_location(packet_index, &location);
    if (status != BOARD_OK) {
        storage_is_full = 1U;
        return status;
    }

    storage_next_packet_index = packet_index;
    storage_next_block = location.block;
    storage_next_page = location.page;
    storage_is_full = 0U;

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

static BoardStatus storage_advance_after_accept(void) {
    uint32_t next_index;

    next_index = storage_next_packet_index + 1U;

    if (next_index >= NAND_STORAGE_MAX_PHYSICAL_PACKETS) {
        storage_next_packet_index = next_index;
        storage_is_full = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if (storage_update_next_location(next_index) != BOARD_OK) {
        storage_next_packet_index = next_index;
        storage_is_full = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

static void storage_prepare_page_buffer(uint8_t buffer_index,
                                        const void* packet) {
    uint32_t index;
    uint8_t* page;

    page = storage_page_buffers[buffer_index];

    memcpy(page, packet, NAND_STORAGE_PACKET_SIZE);

    for (index = NAND_STORAGE_PACKET_SIZE; index < NAND_MT29F_PAGE_SIZE; ++index) {
        page[index] = 0xFFU;
    }
}

static BoardStatus storage_get_free_buffer(uint8_t* buffer_index) {
    uint8_t used0;
    uint8_t used1;

    if (buffer_index == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    used0 = 0U;
    used1 = 0U;

    if (storage_active_write.valid != 0U) {
        if (storage_active_write.buffer_index == 0U) {
            used0 = 1U;
        } else {
            used1 = 1U;
        }
    }

    if (storage_queued_write.valid != 0U) {
        if (storage_queued_write.buffer_index == 0U) {
            used0 = 1U;
        } else {
            used1 = 1U;
        }
    }

    if (used0 == 0U) {
        *buffer_index = 0U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if (used1 == 0U) {
        *buffer_index = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    storage_set_status(BOARD_ERR_BUSY);

    return BOARD_ERR_BUSY;
}

static BoardStatus storage_start_prepared_write(const NandStorageWriteRequest* request) {
    BoardStatus status;

    if (request == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_program_page_dma_start(request->location.block,
                                               request->location.page,
                                               storage_page_buffers[request->buffer_index],
                                               NAND_MT29F_PAGE_SIZE);
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    storage_active_write = *request;
    storage_active_write.valid = 1U;

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

static BoardStatus storage_start_queued_if_present(void) {
    BoardStatus status;
    NandStorageWriteRequest request;

    if (storage_queued_write.valid == 0U) {
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    request = storage_queued_write;
    storage_queued_write.valid = 0U;

    status = storage_start_prepared_write(&request);
    if (status != BOARD_OK) {
        storage_mode = NAND_STORAGE_MODE_ERROR;
        storage_set_status(status);
        return status;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_init(void) {
    storage_mounted = 0U;
    storage_bank = NAND_MT29F_BANK_1;
    storage_clear_bad_block_cache();
    storage_reset_runtime_state();

    return BOARD_OK;
}

BoardStatus nand_storage_mount(NandMt29fBank bank) {
    BoardStatus status;

    if (storage_bank_is_valid(bank) == 0U) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    if ((storage_mounted == 0U) || (storage_bank != bank)) {
        storage_clear_bad_block_cache();
        storage_reset_runtime_state();
    }

    status = nand_mt29f_select_bank(bank);
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    status = nand_mt29f_init();
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    storage_bank = bank;
    storage_mounted = 1U;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_close(void) {
    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    storage_mode = NAND_STORAGE_MODE_IDLE;
    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_capacity_packets(uint32_t* packet_capacity) {
    BoardStatus status;
    uint32_t block;
    uint32_t capacity;
    uint8_t is_bad;

    if (packet_capacity == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    capacity = 0U;

    for (block = 0U; block < NAND_MT29F_BLOCKS_PER_LUN; ++block) {
        status = storage_get_block_bad_state(block, &is_bad);
        if (status != BOARD_OK) {
            return status;
        }

        if (is_bad == 0U) {
            capacity += NAND_MT29F_PAGES_PER_BLOCK;
        }
    }

    *packet_capacity = capacity;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_committed_packet_count(uint32_t* packet_count) {
    if (packet_count == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    *packet_count = storage_committed_packet_count;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_open_write(NandMt29fBank bank,
                                    uint32_t start_packet_count) {
    BoardStatus status;

    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    storage_clear_write_requests();

    storage_committed_packet_count = start_packet_count;
    storage_next_packet_index = start_packet_count;
    storage_is_full = 0U;

    if (storage_update_next_location(start_packet_count) != BOARD_OK) {
        storage_is_full = 1U;
    }

    storage_mode = NAND_STORAGE_MODE_WRITE;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_write_packet(const void* packet) {
    BoardStatus status;
    uint8_t buffer_index;
    NandStorageWriteRequest request;

    if (packet == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (storage_mode != NAND_STORAGE_MODE_WRITE) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = nand_storage_write_poll(0);
    if (status != BOARD_OK) {
        return status;
    }

    if (storage_is_full != 0U) {
        storage_set_status(BOARD_ERR_IO);
        return BOARD_ERR_IO;
    }

    if ((storage_active_write.valid != 0U) &&
        (storage_queued_write.valid != 0U)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = storage_get_free_buffer(&buffer_index);
    if (status != BOARD_OK) {
        return status;
    }

    storage_prepare_page_buffer(buffer_index, packet);

    request.valid = 1U;
    request.buffer_index = buffer_index;
    request.packet_index = storage_next_packet_index;
    request.location.block = storage_next_block;
    request.location.page = storage_next_page;

    if (storage_active_write.valid != 0U) {
        storage_queued_write = request;
        status = storage_advance_after_accept();
        if (status != BOARD_OK) {
            return status;
        }

        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = storage_start_prepared_write(&request);
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_advance_after_accept();
    if (status != BOARD_OK) {
        return status;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_write_poll(uint8_t* is_idle) {
    BoardStatus status;
    uint8_t is_done;

    if (is_idle != 0) {
        *is_idle = 0U;
    }

    if (storage_mode != NAND_STORAGE_MODE_WRITE) {
        if (is_idle != 0) {
            *is_idle = 1U;
        }

        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if (storage_active_write.valid == 0U) {
        if (storage_queued_write.valid != 0U) {
            status = storage_start_queued_if_present();
            if (status != BOARD_OK) {
                return status;
            }
        } else {
            if (is_idle != 0) {
                *is_idle = 1U;
            }

            storage_set_status(BOARD_OK);
            return BOARD_OK;
        }
    }

    is_done = 0U;

    status = nand_mt29f_program_page_dma_poll(&is_done);
    if (status != BOARD_OK) {
        storage_mark_block_bad(storage_active_write.location.block);
        storage_active_write.valid = 0U;
        storage_queued_write.valid = 0U;
        storage_mode = NAND_STORAGE_MODE_ERROR;
        storage_set_status(status);
        return status;
    }

    if (is_done == 0U) {
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if (storage_committed_packet_count <= storage_active_write.packet_index) {
        storage_committed_packet_count = storage_active_write.packet_index + 1U;
    }

    storage_active_write.valid = 0U;

    status = storage_start_queued_if_present();
    if (status != BOARD_OK) {
        return status;
    }

    if ((storage_active_write.valid == 0U) &&
        (storage_queued_write.valid == 0U)) {
        if (is_idle != 0) {
            *is_idle = 1U;
        }
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_write_flush(uint8_t* is_done) {
    return nand_storage_write_poll(is_done);
}

BoardStatus nand_storage_open_read(NandMt29fBank bank,
                                   uint32_t packet_count) {
    BoardStatus status;

    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    storage_read_packet_count = packet_count;
    storage_read_next_packet_index = 0U;
    storage_mode = NAND_STORAGE_MODE_READ;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_read_packet(uint32_t packet_index,
                                     void* packet) {
    BoardStatus status;
    NandStorageLocation location;

    if (packet == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if ((storage_mode != NAND_STORAGE_MODE_READ) &&
        (storage_mode != NAND_STORAGE_MODE_IDLE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    if (storage_mode == NAND_STORAGE_MODE_READ) {
        if (packet_index >= storage_read_packet_count) {
            storage_set_status(BOARD_ERR_INVALID_ARG);
            return BOARD_ERR_INVALID_ARG;
        }
    }

    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_find_packet_location(packet_index, &location);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_read_page(location.block,
                                  location.page,
                                  storage_read_page_buffer,
                                  NAND_MT29F_PAGE_SIZE);
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    memcpy(packet, storage_read_page_buffer, NAND_STORAGE_PACKET_SIZE);

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_read_next_packet(void* packet,
                                          uint8_t* has_packet) {
    BoardStatus status;

    if (has_packet == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    *has_packet = 0U;

    if (storage_mode != NAND_STORAGE_MODE_READ) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    if (storage_read_next_packet_index >= storage_read_packet_count) {
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = nand_storage_read_packet(storage_read_next_packet_index, packet);
    if (status != BOARD_OK) {
        return status;
    }

    ++storage_read_next_packet_index;
    *has_packet = 1U;

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_erase_range_start(NandMt29fBank bank,
                                           uint32_t first_block,
                                           uint32_t block_count) {
    BoardStatus status;

    if (storage_bank_is_valid(bank) == 0U) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (first_block >= NAND_MT29F_BLOCKS_PER_LUN) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (block_count == 0U) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if (block_count > (NAND_MT29F_BLOCKS_PER_LUN - first_block)) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    if ((storage_active_write.valid != 0U) ||
        (storage_queued_write.valid != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    storage_clear_write_requests();

    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;

    storage_erase_start_block = first_block;
    storage_erase_next_block = first_block;
    storage_erase_end_block = first_block + block_count;
    storage_erase_erased_blocks = 0U;
    storage_erase_skipped_bad_blocks = 0U;
    storage_erase_failed_blocks = 0U;

    if (first_block == 0U) {
        storage_committed_packet_count = 0U;
        storage_next_packet_index = 0U;
        storage_next_block = 0U;
        storage_next_page = 0U;
        storage_is_full = 0U;
    }

    storage_mode = NAND_STORAGE_MODE_ERASE;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_erase_bank_start(NandMt29fBank bank) {
    return nand_storage_erase_range_start(bank,
                                          0U,
                                          NAND_MT29F_BLOCKS_PER_LUN);
}

BoardStatus nand_storage_erase_poll(uint8_t* is_done) {
    BoardStatus status;
    uint32_t block;
    uint8_t is_bad;

    if (is_done == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    *is_done = 0U;

    if (storage_mode != NAND_STORAGE_MODE_ERASE) {
        *is_done = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        storage_mode = NAND_STORAGE_MODE_ERROR;
        return status;
    }

    if (storage_erase_next_block >= storage_erase_end_block) {
        storage_mode = NAND_STORAGE_MODE_IDLE;

        if (storage_erase_start_block == 0U) {
            storage_is_full = 0U;
            storage_committed_packet_count = 0U;
            storage_next_packet_index = 0U;
            (void)storage_update_next_location(0U);
        }

        *is_done = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    block = storage_erase_next_block;
    ++storage_erase_next_block;

    status = storage_get_block_bad_state(block, &is_bad);
    if (status != BOARD_OK) {
        storage_mode = NAND_STORAGE_MODE_ERROR;
        return status;
    }

    if (is_bad != 0U) {
        ++storage_erase_skipped_bad_blocks;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = nand_mt29f_erase_block(block);
    if (status != BOARD_OK) {
        storage_mark_block_bad(block);
        ++storage_erase_failed_blocks;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    ++storage_erase_erased_blocks;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_erase_bank_poll(uint8_t* is_done) {
    return nand_storage_erase_poll(is_done);
}

BoardStatus nand_storage_is_full(uint8_t* is_full) {
    if (is_full == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    *is_full = storage_is_full;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_info(NandStorageInfo* info) {
    if (info == 0) {
        storage_set_status(BOARD_ERR_INVALID_ARG);
        return BOARD_ERR_INVALID_ARG;
    }

    info->mode = storage_mode;
    info->bank = storage_bank;
    info->mounted = storage_mounted;
    info->is_full = storage_is_full;
    info->write_active = storage_active_write.valid;
    info->write_queued = storage_queued_write.valid;
    info->last_status = storage_last_status;
    info->committed_packet_count = storage_committed_packet_count;
    info->next_packet_index = storage_next_packet_index;
    info->next_block = storage_next_block;
    info->next_page = storage_next_page;
    info->read_packet_count = storage_read_packet_count;
    info->read_next_packet_index = storage_read_next_packet_index;
    info->erase_start_block = storage_erase_start_block;
    info->erase_next_block = storage_erase_next_block;
    info->erase_end_block = storage_erase_end_block;
    info->erase_erased_blocks = storage_erase_erased_blocks;
    info->erase_skipped_bad_blocks = storage_erase_skipped_bad_blocks;
    info->erase_failed_blocks = storage_erase_failed_blocks;

    return BOARD_OK;
}