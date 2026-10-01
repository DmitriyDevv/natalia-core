#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_nand_mt29f.h"
#include "nand_storage.h"

#define BANK NAND_MT29F_BANK_1
#define BANK_ID 1U
#define FIRST_SESSION_PACKETS 513U
#define TOTAL_PACKETS 1024U
#define POLL_GUARD 100000U

static uint8_t packet[NAND_STORAGE_PACKET_SIZE];
static uint8_t readback[NAND_STORAGE_PACKET_SIZE];
static NandStorageBlockMap map;

static void fill_packet(uint32_t index) {
    (void)memset(packet, 0, sizeof(packet));
    (void)memcpy(packet, &index, sizeof(index));
}

static uint32_t token_of(const uint8_t* buffer) {
    uint32_t token = 0U;

    (void)memcpy(&token, buffer, sizeof(token));

    return token;
}

static void mount_and_scan(void) {
    uint8_t is_done = 0U;
    uint32_t guard;

    assert(nand_storage_init() == BOARD_OK);
    assert(nand_storage_mount(BANK) == BOARD_OK);
    assert(nand_storage_scan_start(BANK) == BOARD_OK);

    for (guard = 0U; (guard < POLL_GUARD) && (is_done == 0U); ++guard) {
        assert(nand_storage_scan_poll(&is_done) == BOARD_OK);
    }
    assert(is_done != 0U);

    assert(nand_storage_get_block_map(BANK, &map) == BOARD_OK);
}

static void erase_bank(void) {
    uint8_t is_done = 0U;
    uint32_t guard;

    assert(nand_storage_erase_bank_start(BANK) == BOARD_OK);

    for (guard = 0U; (guard < POLL_GUARD) && (is_done == 0U); ++guard) {
        assert(nand_storage_erase_bank_poll(&is_done) == BOARD_OK);
    }
    assert(is_done != 0U);
}

static void write_session(uint32_t first, uint32_t count) {
    uint32_t index;
    uint8_t is_idle = 0U;
    uint32_t guard;
    BoardStatus status;

    assert(nand_storage_open_write(BANK, first) == BOARD_OK);

    for (index = first; index < (first + count); ++index) {
        fill_packet(index);

        for (guard = 0U; guard < POLL_GUARD; ++guard) {
            status = nand_storage_write_packet(packet);
            if (status != BOARD_ERR_BUSY) {
                break;
            }
            assert(nand_storage_write_poll(&is_idle) == BOARD_OK);
        }
        assert(status == BOARD_OK);
    }

    is_idle = 0U;
    for (guard = 0U; (guard < POLL_GUARD) && (is_idle == 0U); ++guard) {
        assert(nand_storage_write_flush(&is_idle) == BOARD_OK);
    }
    assert(is_idle != 0U);
}

static uint32_t count_blocks_not_erased(void) {
    uint32_t block;
    uint32_t not_erased = 0U;

    for (block = 0U; block < NAND_MT29F_BLOCKS_PER_LUN; ++block) {
        if (fake_nand_block_is_erased(BANK_ID, block) == 0U) {
            if (not_erased == 0U) {
                printf("first block not erased: %u\n", block);
            }
            ++not_erased;
        }
    }

    return not_erased;
}

static void full_bank_erase_clears_every_block(void) {
    uint32_t block;
    uint32_t not_erased;
    FakeNandStats stats;

    fake_nand_reset();
    mount_and_scan();

    /* Make every block dirty, exactly like the bench test does. */
    for (block = 0U; block < NAND_MT29F_BLOCKS_PER_LUN; ++block) {
        assert(nand_storage_open_write(BANK, block * NAND_STORAGE_PACKETS_PER_BLOCK) == BOARD_OK);
        fill_packet(block);
        assert(nand_storage_write_packet(packet) == BOARD_OK);
        {
            uint8_t is_idle = 0U;
            uint32_t guard;

            for (guard = 0U; (guard < POLL_GUARD) && (is_idle == 0U); ++guard) {
                assert(nand_storage_write_flush(&is_idle) == BOARD_OK);
            }
        }
    }

    assert(count_blocks_not_erased() == NAND_MT29F_BLOCKS_PER_LUN);

    erase_bank();

    stats = fake_nand_stats();
    printf("erase calls: %u\n", stats.erase_calls);
    not_erased = count_blocks_not_erased();
    printf("blocks not erased after bank erase: %u\n", not_erased);

    assert(stats.erase_calls == NAND_MT29F_BLOCKS_PER_LUN);
    assert(not_erased == 0U);
}

static void two_write_sessions_keep_every_packet(void) {
    uint32_t index;
    uint32_t mismatches = 0U;

    fake_nand_reset();
    mount_and_scan();
    erase_bank();

    write_session(0U, FIRST_SESSION_PACKETS);

    /* The untouched half of the page holding packet 513 must still be erased. */
    assert(nand_storage_open_read(BANK, FIRST_SESSION_PACKETS + 1U) == BOARD_OK);
    assert(nand_storage_read_packet(FIRST_SESSION_PACKETS, readback) == BOARD_OK);
    assert(token_of(readback) == 0xFFFFFFFFU);

    write_session(FIRST_SESSION_PACKETS, TOTAL_PACKETS - FIRST_SESSION_PACKETS);

    assert(nand_storage_open_read(BANK, TOTAL_PACKETS) == BOARD_OK);
    for (index = 0U; index < TOTAL_PACKETS; ++index) {
        assert(nand_storage_read_packet(index, readback) == BOARD_OK);
        if (token_of(readback) != index) {
            if (mismatches < 8U) {
                printf("packet %u read back as %u\n", index, token_of(readback));
            }
            ++mismatches;
        }
    }

    printf("mismatched packets: %u\n", mismatches);
    assert(mismatches == 0U);
}

static void bad_blocks_are_skipped(void) {
    uint32_t index;

    fake_nand_reset();
    fake_nand_set_factory_bad(BANK_ID, 0U);
    fake_nand_set_factory_bad(BANK_ID, 5U);
    mount_and_scan();
    erase_bank();

    write_session(0U, NAND_STORAGE_PACKETS_PER_BLOCK * 8U);

    assert(nand_storage_open_read(BANK, NAND_STORAGE_PACKETS_PER_BLOCK * 8U) == BOARD_OK);
    for (index = 0U; index < (NAND_STORAGE_PACKETS_PER_BLOCK * 8U); ++index) {
        assert(nand_storage_read_packet(index, readback) == BOARD_OK);
        assert(token_of(readback) == index);
    }

    assert(fake_nand_block_is_erased(BANK_ID, 0U) != 0U);
    assert(fake_nand_block_is_erased(BANK_ID, 5U) != 0U);
}

static uint8_t map_bit(const uint8_t* bits, uint32_t block) {
    return (uint8_t)((bits[block >> 3U] >> (block & 7U)) & 1U);
}

static void ignored_erase_is_reported_but_not_retired(void) {
    NandStorageInfo info;

    fake_nand_reset();
    fake_nand_set_erase_ignored(BANK_ID, 7U);
    fake_nand_set_erase_ignored(BANK_ID, 9U);
    mount_and_scan();
    erase_bank();

    assert(nand_storage_get_info(&info) == BOARD_OK);
    assert(info.erase_error_blocks == 2U);
    assert(info.erase_failed_blocks == 0U);
    assert(info.erase_erased_blocks == (NAND_MT29F_BLOCKS_PER_LUN - 2U));
    assert(info.good_blocks == NAND_MT29F_BLOCKS_PER_LUN);
    assert(info.fault_count == 2U);
    assert(info.first_fault_block == 7U);
    assert(info.first_fault == NAND_MT29F_FAULT_BUSY_NOT_SEEN);
    assert(info.first_fault_status == BOARD_ERR_IO);
    assert(info.last_fault_block == 9U);

    assert(nand_storage_get_block_map(BANK, &map) == BOARD_OK);
    assert(map_bit(map.bad, 7U) == 0U);
    assert(map_bit(map.bad, 9U) == 0U);
}

static void erase_fail_retires_block(void) {
    NandStorageInfo info;

    fake_nand_reset();
    fake_nand_set_erase_fail(BANK_ID, 11U);
    mount_and_scan();
    erase_bank();

    assert(nand_storage_get_info(&info) == BOARD_OK);
    assert(info.erase_failed_blocks == 1U);
    assert(info.erase_error_blocks == 0U);
    assert(info.good_blocks == (NAND_MT29F_BLOCKS_PER_LUN - 1U));
    assert(info.first_fault_block == 11U);
    assert(info.first_fault == NAND_MT29F_FAULT_ERASE_FAIL);
    assert(info.first_fault_chip_status == 0x04U);

    assert(nand_storage_get_block_map(BANK, &map) == BOARD_OK);
    assert(map_bit(map.bad, 11U) == 1U);
}

static void program_fail_marks_candidate(void) {
    NandStorageInfo info;
    uint8_t is_idle = 0U;
    uint32_t guard;
    BoardStatus status = BOARD_OK;

    fake_nand_reset();
    fake_nand_set_program_fail(BANK_ID, 0U);
    mount_and_scan();
    erase_bank();

    assert(nand_storage_open_write(BANK, 0U) == BOARD_OK);
    fill_packet(0U);
    assert(nand_storage_write_packet(packet) == BOARD_OK);

    for (guard = 0U; (guard < POLL_GUARD) && (is_idle == 0U); ++guard) {
        status = nand_storage_write_flush(&is_idle);
        if (status != BOARD_OK) {
            break;
        }
    }

    assert(status == BOARD_ERR_IO);
    assert(nand_storage_get_info(&info) == BOARD_OK);
    assert(info.first_fault == NAND_MT29F_FAULT_PROGRAM_FAIL);
    assert(info.first_fault_block == 0U);

    assert(nand_storage_get_block_map(BANK, &map) == BOARD_OK);
    assert(map_bit(map.candidate, 0U) == 1U);
}

int main(void) {
    full_bank_erase_clears_every_block();
    two_write_sessions_keep_every_packet();
    bad_blocks_are_skipped();
    ignored_erase_is_reported_but_not_retired();
    erase_fail_retires_block();
    program_fail_marks_candidate();

    printf("test_nand_storage passed\n");

    return 0;
}
