#ifndef NATALIA_NAND_STORAGE_H
#define NATALIA_NAND_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "nand_mt29f.h"
#include "status.h"

#define NAND_STORAGE_PACKET_SIZE 2048UL
#define NAND_STORAGE_PACKETS_PER_PAGE (NAND_MT29F_PAGE_SIZE / NAND_STORAGE_PACKET_SIZE)
#define NAND_STORAGE_PACKETS_PER_BLOCK (NAND_MT29F_PAGES_PER_BLOCK * NAND_STORAGE_PACKETS_PER_PAGE)
#define NAND_STORAGE_MAX_PHYSICAL_PACKETS (NAND_MT29F_BLOCKS_PER_LUN * NAND_STORAGE_PACKETS_PER_BLOCK)
#define NAND_STORAGE_BLOCK_MAP_BYTES (NAND_MT29F_BLOCKS_PER_LUN / 8UL)

typedef enum {
    NAND_STORAGE_MODE_IDLE = 0,
    NAND_STORAGE_MODE_READ = 1,
    NAND_STORAGE_MODE_WRITE = 2,
    NAND_STORAGE_MODE_ERASE = 3,
    NAND_STORAGE_MODE_ERROR = 4,
    NAND_STORAGE_MODE_SCAN = 5
} NandStorageMode;

typedef struct {
    uint8_t bad[NAND_STORAGE_BLOCK_MAP_BYTES];
    uint8_t candidate[NAND_STORAGE_BLOCK_MAP_BYTES];
} NandStorageBlockMap;

typedef struct {
    NandStorageMode mode;
    NandMt29fBank bank;
    uint8_t mounted;
    uint8_t map_valid;
    uint8_t is_full;
    uint8_t write_active;
    uint8_t write_queued;
    BoardStatus last_status;
    uint32_t good_blocks;
    uint32_t capacity_packets;
    uint32_t committed_packet_count;
    uint32_t next_packet_index;
    uint32_t read_packet_count;
    uint32_t read_next_packet_index;
    uint32_t erase_next_block;
    uint32_t erase_erased_blocks;
    uint32_t erase_skipped_bad_blocks;
    uint32_t erase_failed_blocks;
    uint32_t erase_error_blocks;
    uint32_t program_done;
    uint32_t program_unconfirmed;
    uint32_t scan_next_block;
    uint32_t fault_count;
    uint32_t first_fault_block;
    NandMt29fFault first_fault;
    BoardStatus first_fault_status;
    uint8_t first_fault_chip_status;
    uint32_t last_fault_block;
    NandMt29fFault last_fault;
    BoardStatus last_fault_status;
    uint8_t last_fault_chip_status;
} NandStorageInfo;

BoardStatus nand_storage_init(void);

BoardStatus nand_storage_mount(NandMt29fBank bank);

BoardStatus nand_storage_close(void);

BoardStatus nand_storage_set_block_map(NandMt29fBank bank, const NandStorageBlockMap* map);

BoardStatus nand_storage_get_block_map(NandMt29fBank bank, NandStorageBlockMap* map);

BoardStatus nand_storage_scan_start(NandMt29fBank bank);

BoardStatus nand_storage_scan_poll(uint8_t* is_done);

BoardStatus nand_storage_get_capacity_packets(uint32_t* packet_capacity);

BoardStatus nand_storage_get_committed_packet_count(uint32_t* packet_count);

BoardStatus nand_storage_open_write(NandMt29fBank bank,
                                    uint32_t start_packet_count);

BoardStatus nand_storage_write_packet(const void* packet);

BoardStatus nand_storage_write_poll(uint8_t* is_idle);

BoardStatus nand_storage_write_flush(uint8_t* is_done);

BoardStatus nand_storage_open_read(NandMt29fBank bank,
                                   uint32_t packet_count);

BoardStatus nand_storage_read_packet(uint32_t packet_index,
                                     void* packet);

BoardStatus nand_storage_read_next_packet(void* packet,
                                          uint8_t* has_packet);

BoardStatus nand_storage_erase_bank_start(NandMt29fBank bank);

BoardStatus nand_storage_erase_bank_poll(uint8_t* is_done);

BoardStatus nand_storage_is_full(uint8_t* is_full);

BoardStatus nand_storage_get_info(NandStorageInfo* info);

#endif
