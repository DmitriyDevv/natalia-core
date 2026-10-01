#include "nand_storage.h"

#include <stdint.h>
#include <string.h>

#define NAND_STORAGE_BANK_COUNT 2U
#define NAND_STORAGE_SCAN_BLOCKS_PER_POLL 16U
#define NAND_STORAGE_NO_BANK 0xFFU

typedef struct {
    uint8_t valid;
    uint8_t buffer_index;
    uint32_t packet_index;
    uint32_t block;
    uint32_t page;
    uint32_t column;
} NandStorageWriteRequest;

static uint8_t storage_write_buffers[2U][NAND_STORAGE_PACKET_SIZE] __attribute__((aligned(4)));
static uint8_t storage_read_buffer[NAND_STORAGE_PACKET_SIZE] __attribute__((aligned(4)));

static NandStorageBlockMap storage_maps[NAND_STORAGE_BANK_COUNT];
static uint8_t storage_map_valid[NAND_STORAGE_BANK_COUNT];
static uint16_t storage_good_blocks[NAND_MT29F_BLOCKS_PER_LUN];
static uint32_t storage_good_block_count = 0U;
static uint8_t storage_good_table_bank = NAND_STORAGE_NO_BANK;

static NandStorageMode storage_mode = NAND_STORAGE_MODE_IDLE;
static NandMt29fBank storage_bank = NAND_MT29F_BANK_1;
static uint8_t storage_mounted = 0U;
static uint8_t storage_is_full = 0U;
static BoardStatus storage_last_status = BOARD_OK;

static uint32_t storage_committed_packet_count = 0U;
static uint32_t storage_next_packet_index = 0U;

static uint32_t storage_read_packet_count = 0U;
static uint32_t storage_read_next_packet_index = 0U;

static uint32_t storage_erase_next_block = 0U;
static uint32_t storage_erase_erased_blocks = 0U;
static uint32_t storage_erase_skipped_bad_blocks = 0U;
static uint32_t storage_erase_failed_blocks = 0U;
static uint32_t storage_erase_error_blocks = 0U;

static uint32_t storage_program_done = 0U;
static uint32_t storage_program_unconfirmed = 0U;

static uint32_t storage_scan_next_block = 0U;

typedef struct {
    uint32_t block;
    NandMt29fFault fault;
    BoardStatus status;
    uint8_t chip_status;
} NandStorageFault;

static uint32_t storage_fault_count = 0U;
static NandStorageFault storage_first_fault;
static NandStorageFault storage_last_fault;

static NandStorageWriteRequest storage_active_write;
static NandStorageWriteRequest storage_queued_write;

#if (NAND_MT29F_PAGE_SIZE % NAND_STORAGE_PACKET_SIZE) != 0
#error "NAND page size must be a multiple of the packet size"
#endif

static void storage_set_status(BoardStatus status) {
    storage_last_status = status;

    if ((status != BOARD_OK) && (status != BOARD_ERR_BUSY) &&
        (status != BOARD_ERR_NOT_READY) && (status != BOARD_ERR_INVALID_ARG)) {
        storage_mode = NAND_STORAGE_MODE_ERROR;
    }
}

static BoardStatus storage_fail(BoardStatus status) {
    storage_set_status(status);
    return status;
}

static uint8_t storage_bank_index(NandMt29fBank bank, uint8_t* index) {
    if (bank == NAND_MT29F_BANK_1) {
        *index = 0U;
        return 1U;
    }

    if (bank == NAND_MT29F_BANK_2) {
        *index = 1U;
        return 1U;
    }

    return 0U;
}

static uint8_t storage_mounted_index(void) {
    uint8_t index = 0U;

    (void)storage_bank_index(storage_bank, &index);

    return index;
}

static uint8_t storage_map_bit(const uint8_t* bits, uint32_t block) {
    return (uint8_t)((bits[block >> 3U] >> (block & 7U)) & 1U);
}

static void storage_map_set_bit(uint8_t* bits, uint32_t block) {
    bits[block >> 3U] = (uint8_t)(bits[block >> 3U] | (uint8_t)(1U << (block & 7U)));
}

static uint32_t storage_capacity(void) {
    return storage_good_block_count * NAND_STORAGE_PACKETS_PER_BLOCK;
}

static void storage_rebuild_good_table(uint8_t index) {
    uint32_t block;

    storage_good_block_count = 0U;

    for (block = 0U; block < NAND_MT29F_BLOCKS_PER_LUN; ++block) {
        if (storage_map_bit(storage_maps[index].bad, block) == 0U) {
            storage_good_blocks[storage_good_block_count] = (uint16_t)block;
            ++storage_good_block_count;
        }
    }

    storage_good_table_bank = index;
}

static BoardStatus storage_require_map(void) {
    uint8_t index = storage_mounted_index();

    if ((storage_mounted == 0U) || (storage_map_valid[index] == 0U)) {
        storage_set_status(BOARD_ERR_NOT_READY);
        return BOARD_ERR_NOT_READY;
    }

    if (storage_good_table_bank != index) {
        storage_rebuild_good_table(index);
    }

    return BOARD_OK;
}

static BoardStatus storage_locate(uint32_t packet_index, NandStorageWriteRequest* location) {
    uint32_t logical_block = packet_index / NAND_STORAGE_PACKETS_PER_BLOCK;
    uint32_t in_block = packet_index % NAND_STORAGE_PACKETS_PER_BLOCK;

    if (logical_block >= storage_good_block_count) {
        return BOARD_ERR_INVALID_ARG;
    }

    location->block = storage_good_blocks[logical_block];
    location->page = in_block / NAND_STORAGE_PACKETS_PER_PAGE;
    location->column = (in_block % NAND_STORAGE_PACKETS_PER_PAGE) * NAND_STORAGE_PACKET_SIZE;
    location->packet_index = packet_index;

    return BOARD_OK;
}

static void storage_clear_faults(void) {
    storage_fault_count = 0U;
    (void)memset(&storage_first_fault, 0, sizeof(storage_first_fault));
    (void)memset(&storage_last_fault, 0, sizeof(storage_last_fault));
}

static uint8_t storage_record_fault(uint32_t block, BoardStatus status) {
    NandStorageFault fault;

    fault.block = block;
    fault.fault = nand_mt29f_get_last_fault();
    fault.status = status;
    fault.chip_status = nand_mt29f_get_last_fault_status();

    if (storage_fault_count == 0U) {
        storage_first_fault = fault;
    }

    storage_last_fault = fault;
    ++storage_fault_count;

    return ((fault.fault == NAND_MT29F_FAULT_ERASE_FAIL) ||
            (fault.fault == NAND_MT29F_FAULT_PROGRAM_FAIL)) ? 1U : 0U;
}

static void storage_clear_write_requests(void) {
    (void)memset(&storage_active_write, 0, sizeof(storage_active_write));
    (void)memset(&storage_queued_write, 0, sizeof(storage_queued_write));
}

static uint8_t storage_write_in_flight(void) {
    return ((storage_active_write.valid != 0U) || (storage_queued_write.valid != 0U)) ? 1U : 0U;
}

static void storage_reset_runtime_state(void) {
    storage_mode = NAND_STORAGE_MODE_IDLE;
    storage_is_full = 0U;
    storage_last_status = BOARD_OK;
    storage_committed_packet_count = 0U;
    storage_next_packet_index = 0U;
    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;
    storage_erase_next_block = 0U;
    storage_erase_erased_blocks = 0U;
    storage_erase_skipped_bad_blocks = 0U;
    storage_erase_failed_blocks = 0U;
    storage_erase_error_blocks = 0U;
    storage_program_done = 0U;
    storage_program_unconfirmed = 0U;
    storage_scan_next_block = 0U;
    storage_clear_faults();
    storage_clear_write_requests();
}

static BoardStatus storage_select_mounted_bank(void) {
    BoardStatus status;

    if (storage_mounted == 0U) {
        return storage_fail(BOARD_ERR_IO);
    }

    status = nand_mt29f_select_bank(storage_bank);
    storage_set_status(status);

    return status;
}

static BoardStatus storage_start_write(const NandStorageWriteRequest* request) {
    BoardStatus status;

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_program_page_dma_start_at(request->block,
                                                  request->page,
                                                  request->column,
                                                  storage_write_buffers[request->buffer_index],
                                                  NAND_STORAGE_PACKET_SIZE);
    if (status != BOARD_OK) {
        if (status != BOARD_ERR_BUSY) {
            (void)storage_record_fault(request->block, status);
        }
        return storage_fail(status);
    }

    storage_active_write = *request;
    storage_active_write.valid = 1U;

    return BOARD_OK;
}

static BoardStatus storage_get_free_buffer(uint8_t* buffer_index) {
    uint8_t used[2] = {0U, 0U};

    if (storage_active_write.valid != 0U) {
        used[storage_active_write.buffer_index] = 1U;
    }

    if (storage_queued_write.valid != 0U) {
        used[storage_queued_write.buffer_index] = 1U;
    }

    if (used[0] == 0U) {
        *buffer_index = 0U;
        return BOARD_OK;
    }

    if (used[1] == 0U) {
        *buffer_index = 1U;
        return BOARD_OK;
    }

    return BOARD_ERR_BUSY;
}

BoardStatus nand_storage_init(void) {
    storage_mounted = 0U;
    storage_bank = NAND_MT29F_BANK_1;
    storage_good_table_bank = NAND_STORAGE_NO_BANK;
    storage_good_block_count = 0U;
    (void)memset(storage_maps, 0, sizeof(storage_maps));
    (void)memset(storage_map_valid, 0, sizeof(storage_map_valid));
    storage_reset_runtime_state();

    return BOARD_OK;
}

BoardStatus nand_storage_mount(NandMt29fBank bank) {
    uint8_t index = 0U;
    BoardStatus status;

    if (storage_bank_index(bank, &index) == 0U) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    if ((storage_write_in_flight() != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE) ||
        (storage_mode == NAND_STORAGE_MODE_SCAN)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    if ((storage_mounted == 0U) || (storage_bank != bank)) {
        storage_reset_runtime_state();
    }

    status = nand_mt29f_select_bank(bank);
    if (status != BOARD_OK) {
        return storage_fail(status);
    }

    status = nand_mt29f_init();
    if (status != BOARD_OK) {
        return storage_fail(status);
    }

    storage_bank = bank;
    storage_mounted = 1U;

    if (storage_map_valid[index] != 0U) {
        storage_rebuild_good_table(index);
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_close(void) {
    if ((storage_write_in_flight() != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE) ||
        (storage_mode == NAND_STORAGE_MODE_SCAN)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    storage_mode = NAND_STORAGE_MODE_IDLE;
    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_set_block_map(NandMt29fBank bank, const NandStorageBlockMap* map) {
    uint8_t index = 0U;

    if ((map == 0) || (storage_bank_index(bank, &index) == 0U)) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    if ((storage_write_in_flight() != 0U) ||
        (storage_mode == NAND_STORAGE_MODE_ERASE) ||
        (storage_mode == NAND_STORAGE_MODE_SCAN)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    storage_maps[index] = *map;
    storage_map_valid[index] = 1U;

    if ((storage_mounted != 0U) && (storage_bank == bank)) {
        storage_rebuild_good_table(index);
    } else if (storage_good_table_bank == index) {
        storage_good_table_bank = NAND_STORAGE_NO_BANK;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_block_map(NandMt29fBank bank, NandStorageBlockMap* map) {
    uint8_t index = 0U;

    if ((map == 0) || (storage_bank_index(bank, &index) == 0U)) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    if (storage_map_valid[index] == 0U) {
        storage_set_status(BOARD_ERR_NOT_READY);
        return BOARD_ERR_NOT_READY;
    }

    *map = storage_maps[index];
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_scan_start(NandMt29fBank bank) {
    uint8_t index = 0U;
    BoardStatus status;

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    (void)storage_bank_index(bank, &index);

    (void)memset(&storage_maps[index], 0, sizeof(storage_maps[index]));
    storage_map_valid[index] = 0U;
    if (storage_good_table_bank == index) {
        storage_good_table_bank = NAND_STORAGE_NO_BANK;
    }

    storage_scan_next_block = 0U;
    storage_mode = NAND_STORAGE_MODE_SCAN;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_scan_poll(uint8_t* is_done) {
    uint8_t index = storage_mounted_index();
    uint32_t processed;
    uint8_t is_bad;
    BoardStatus status;

    if (is_done == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    *is_done = 0U;

    if (storage_mode != NAND_STORAGE_MODE_SCAN) {
        *is_done = storage_map_valid[index];
        storage_set_status((storage_map_valid[index] != 0U) ? BOARD_OK : BOARD_ERR_NOT_READY);
        return storage_last_status;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    for (processed = 0U;
         (processed < NAND_STORAGE_SCAN_BLOCKS_PER_POLL) &&
         (storage_scan_next_block < NAND_MT29F_BLOCKS_PER_LUN);
         ++processed) {
        is_bad = 0U;
        status = nand_mt29f_is_block_bad(storage_scan_next_block, &is_bad);
        if (status != BOARD_OK) {
            return storage_fail(status);
        }

        if (is_bad != 0U) {
            storage_map_set_bit(storage_maps[index].bad, storage_scan_next_block);
        }

        ++storage_scan_next_block;
    }

    if (storage_scan_next_block >= NAND_MT29F_BLOCKS_PER_LUN) {
        storage_map_valid[index] = 1U;
        storage_rebuild_good_table(index);
        storage_mode = NAND_STORAGE_MODE_IDLE;
        *is_done = 1U;
    }

    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_capacity_packets(uint32_t* packet_capacity) {
    BoardStatus status;

    if (packet_capacity == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    status = storage_require_map();
    if (status != BOARD_OK) {
        return status;
    }

    *packet_capacity = storage_capacity();
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_committed_packet_count(uint32_t* packet_count) {
    if (packet_count == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    *packet_count = storage_committed_packet_count;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_open_write(NandMt29fBank bank,
                                    uint32_t start_packet_count) {
    BoardStatus status;

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_require_map();
    if (status != BOARD_OK) {
        return status;
    }

    storage_clear_write_requests();
    storage_clear_faults();
    storage_program_done = 0U;
    storage_program_unconfirmed = 0U;

    storage_committed_packet_count = start_packet_count;
    storage_next_packet_index = start_packet_count;
    storage_is_full = (start_packet_count >= storage_capacity()) ? 1U : 0U;

    storage_mode = NAND_STORAGE_MODE_WRITE;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_write_packet(const void* packet) {
    NandStorageWriteRequest request;
    uint8_t buffer_index = 0U;
    BoardStatus status;

    if (packet == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
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

    if ((storage_active_write.valid != 0U) && (storage_queued_write.valid != 0U)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = storage_get_free_buffer(&buffer_index);
    if (status != BOARD_OK) {
        storage_set_status(status);
        return status;
    }

    (void)memset(&request, 0, sizeof(request));
    status = storage_locate(storage_next_packet_index, &request);
    if (status != BOARD_OK) {
        storage_is_full = 1U;
        storage_set_status(BOARD_ERR_IO);
        return BOARD_ERR_IO;
    }

    (void)memcpy(storage_write_buffers[buffer_index], packet, NAND_STORAGE_PACKET_SIZE);
    request.valid = 1U;
    request.buffer_index = buffer_index;

    if (storage_active_write.valid != 0U) {
        storage_queued_write = request;
    } else {
        status = storage_start_write(&request);
        if (status != BOARD_OK) {
            return status;
        }
    }

    ++storage_next_packet_index;
    storage_is_full = (storage_next_packet_index >= storage_capacity()) ? 1U : 0U;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_write_poll(uint8_t* is_idle) {
    NandStorageWriteRequest request;
    uint8_t is_done = 0U;
    BoardStatus status;

    if (is_idle != 0) {
        *is_idle = 0U;
    }

    if (storage_mode != NAND_STORAGE_MODE_WRITE) {
        if (is_idle != 0) {
            *is_idle = (storage_write_in_flight() == 0U) ? 1U : 0U;
        }

        if (storage_mode == NAND_STORAGE_MODE_ERROR) {
            return storage_last_status;
        }

        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    if ((storage_active_write.valid == 0U) && (storage_queued_write.valid != 0U)) {
        request = storage_queued_write;
        storage_queued_write.valid = 0U;

        status = storage_start_write(&request);
        if (status != BOARD_OK) {
            storage_clear_write_requests();
            return status;
        }
    }

    if (storage_active_write.valid == 0U) {
        if (is_idle != 0) {
            *is_idle = 1U;
        }

        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = nand_mt29f_program_page_dma_poll(&is_done);
    if (status != BOARD_OK) {
        if (storage_record_fault(storage_active_write.block, status) != 0U) {
            storage_map_set_bit(storage_maps[storage_mounted_index()].candidate,
                                storage_active_write.block);
        }
        storage_clear_write_requests();
        return storage_fail(status);
    }

    if (is_done == 0U) {
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    ++storage_program_done;
    if (nand_mt29f_last_program_unconfirmed() != 0U) {
        ++storage_program_unconfirmed;
    }

    if (storage_committed_packet_count <= storage_active_write.packet_index) {
        storage_committed_packet_count = storage_active_write.packet_index + 1U;
    }

    storage_active_write.valid = 0U;

    if (storage_queued_write.valid != 0U) {
        request = storage_queued_write;
        storage_queued_write.valid = 0U;

        status = storage_start_write(&request);
        if (status != BOARD_OK) {
            storage_clear_write_requests();
            return status;
        }
    }

    if ((is_idle != 0) && (storage_write_in_flight() == 0U)) {
        *is_idle = 1U;
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

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_require_map();
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
    NandStorageWriteRequest location;
    BoardStatus status;

    if (packet == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    if ((storage_mode != NAND_STORAGE_MODE_READ) && (storage_mode != NAND_STORAGE_MODE_IDLE)) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    if ((storage_mode == NAND_STORAGE_MODE_READ) && (packet_index >= storage_read_packet_count)) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    if (storage_write_in_flight() != 0U) {
        storage_set_status(BOARD_ERR_BUSY);
        return BOARD_ERR_BUSY;
    }

    status = storage_require_map();
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_locate(packet_index, &location);
    if (status != BOARD_OK) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_read_page_at(location.block, location.page, location.column,
                                     storage_read_buffer, NAND_STORAGE_PACKET_SIZE);
    if (status != BOARD_OK) {
        return storage_fail(status);
    }

    (void)memcpy(packet, storage_read_buffer, NAND_STORAGE_PACKET_SIZE);
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_read_next_packet(void* packet,
                                          uint8_t* has_packet) {
    BoardStatus status;

    if (has_packet == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
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

BoardStatus nand_storage_erase_bank_start(NandMt29fBank bank) {
    BoardStatus status;

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = storage_require_map();
    if (status != BOARD_OK) {
        return status;
    }

    storage_clear_write_requests();
    storage_read_packet_count = 0U;
    storage_read_next_packet_index = 0U;
    storage_erase_next_block = 0U;
    storage_erase_erased_blocks = 0U;
    storage_erase_skipped_bad_blocks = 0U;
    storage_erase_failed_blocks = 0U;
    storage_erase_error_blocks = 0U;
    storage_clear_faults();

    storage_mode = NAND_STORAGE_MODE_ERASE;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_erase_bank_poll(uint8_t* is_done) {
    NandStorageBlockMap* map;
    uint8_t index = storage_mounted_index();
    uint32_t block;
    uint32_t byte;
    BoardStatus status;

    if (is_done == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    *is_done = 0U;

    if (storage_mode != NAND_STORAGE_MODE_ERASE) {
        *is_done = 1U;
        if (storage_mode == NAND_STORAGE_MODE_ERROR) {
            return storage_last_status;
        }
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    map = &storage_maps[index];

    if (storage_erase_next_block >= NAND_MT29F_BLOCKS_PER_LUN) {
        for (byte = 0U; byte < NAND_STORAGE_BLOCK_MAP_BYTES; ++byte) {
            map->bad[byte] = (uint8_t)(map->bad[byte] | map->candidate[byte]);
            map->candidate[byte] = 0U;
        }

        storage_rebuild_good_table(index);
        storage_committed_packet_count = 0U;
        storage_next_packet_index = 0U;
        storage_is_full = (storage_capacity() == 0U) ? 1U : 0U;
        storage_mode = NAND_STORAGE_MODE_IDLE;
        *is_done = 1U;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = storage_select_mounted_bank();
    if (status != BOARD_OK) {
        return status;
    }

    block = storage_erase_next_block;
    ++storage_erase_next_block;

    if (storage_map_bit(map->bad, block) != 0U) {
        ++storage_erase_skipped_bad_blocks;
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    status = nand_mt29f_erase_block(block);
    if (status != BOARD_OK) {
        if (storage_record_fault(block, status) != 0U) {
            storage_map_set_bit(map->candidate, block);
            ++storage_erase_failed_blocks;
        } else {
            ++storage_erase_error_blocks;
        }
        storage_set_status(BOARD_OK);
        return BOARD_OK;
    }

    ++storage_erase_erased_blocks;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_is_full(uint8_t* is_full) {
    if (is_full == 0) {
        return storage_fail(BOARD_ERR_INVALID_ARG);
    }

    *is_full = storage_is_full;
    storage_set_status(BOARD_OK);

    return BOARD_OK;
}

BoardStatus nand_storage_get_info(NandStorageInfo* info) {
    uint8_t index = storage_mounted_index();

    if (info == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    info->mode = storage_mode;
    info->bank = storage_bank;
    info->mounted = storage_mounted;
    info->map_valid = storage_map_valid[index];
    info->is_full = storage_is_full;
    info->write_active = storage_active_write.valid;
    info->write_queued = storage_queued_write.valid;
    info->last_status = storage_last_status;
    info->good_blocks = (storage_good_table_bank == index) ? storage_good_block_count : 0U;
    info->capacity_packets = (storage_good_table_bank == index) ? storage_capacity() : 0U;
    info->committed_packet_count = storage_committed_packet_count;
    info->next_packet_index = storage_next_packet_index;
    info->read_packet_count = storage_read_packet_count;
    info->read_next_packet_index = storage_read_next_packet_index;
    info->erase_next_block = storage_erase_next_block;
    info->erase_erased_blocks = storage_erase_erased_blocks;
    info->erase_skipped_bad_blocks = storage_erase_skipped_bad_blocks;
    info->erase_failed_blocks = storage_erase_failed_blocks;
    info->erase_error_blocks = storage_erase_error_blocks;
    info->program_done = storage_program_done;
    info->program_unconfirmed = storage_program_unconfirmed;
    info->scan_next_block = storage_scan_next_block;
    info->fault_count = storage_fault_count;
    info->first_fault_block = storage_first_fault.block;
    info->first_fault = storage_first_fault.fault;
    info->first_fault_status = storage_first_fault.status;
    info->first_fault_chip_status = storage_first_fault.chip_status;
    info->last_fault_block = storage_last_fault.block;
    info->last_fault = storage_last_fault.fault;
    info->last_fault_status = storage_last_fault.status;
    info->last_fault_chip_status = storage_last_fault.chip_status;

    return BOARD_OK;
}
