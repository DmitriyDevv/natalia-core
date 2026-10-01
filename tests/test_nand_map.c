#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "board_stub.h"
#include "mram_store.h"
#include "nand_map.h"

static BoardNandBlockMap map;

static bool bit_set(const uint8_t* bits, uint32_t block) {
    return ((bits[block / 8U] >> (block % 8U)) & 1U) != 0U;
}

static void load_mram_map(uint8_t copy_id, uint8_t bank, bool expect_valid) {
    uint8_t is_valid = 0U;

    memset(&map, 0, sizeof(map));
    assert(board_mram_read_block_map(copy_id, bank, &map, &is_valid) == BOARD_OK);
    assert((is_valid != 0U) == expect_valid);
}

static void missing_mram_map_triggers_scan_and_save(void) {
    board_stub_reset_all();
    board_stub_set_nand_factory_bad_block(1U, 7U);
    board_stub_set_nand_factory_bad_block(1U, 2047U);

    assert(nand_map_load(1U) == BOARD_OK);
    assert(board_stub_nand_scan_count(1U) == 1U);
    assert(board_stub_nand_map_is_valid(1U));

    load_mram_map(1U, 1U, true);
    assert(bit_set(map.bad, 7U));
    assert(bit_set(map.bad, 2047U));
    assert(!bit_set(map.bad, 8U));
    load_mram_map(2U, 1U, true);
    assert(bit_set(map.bad, 7U));
    load_mram_map(1U, 2U, false);
}

static void valid_mram_map_is_used_without_scan(void) {
    BoardNandBlockMap stored;
    BoardNandBlockMap driver;

    board_stub_reset_all();
    memset(&stored, 0, sizeof(stored));
    stored.bad[1] = 0x04U;
    stored.candidate[3] = 0x80U;
    assert(mram_store_save_block_map(2U, &stored) == BOARD_OK);

    assert(nand_map_load(2U) == BOARD_OK);
    assert(board_stub_nand_scan_count(2U) == 0U);
    assert(board_nand_get_block_map(2U, &driver) == BOARD_OK);
    assert(memcmp(&driver, &stored, sizeof(stored)) == 0);
}

static void corrupted_copy_is_restored(void) {
    BoardNandBlockMap stored;

    board_stub_reset_all();
    memset(&stored, 0, sizeof(stored));
    stored.bad[0] = 0x01U;
    assert(mram_store_save_block_map(1U, &stored) == BOARD_OK);

    board_stub_set_mram_block_map_valid(1U, 1U, false);
    assert(nand_map_load(1U) == BOARD_OK);
    assert(board_stub_nand_scan_count(1U) == 0U);
    load_mram_map(1U, 1U, true);
    assert(bit_set(map.bad, 0U));

    board_stub_set_mram_block_map_valid(2U, 1U, false);
    assert(nand_map_load(1U) == BOARD_OK);
    load_mram_map(2U, 1U, true);
    assert(bit_set(map.bad, 0U));

    board_stub_set_mram_block_map_valid(1U, 1U, false);
    board_stub_set_mram_block_map_valid(2U, 1U, false);
    assert(nand_map_load(1U) == BOARD_OK);
    assert(board_stub_nand_scan_count(1U) == 1U);
}

static void rebuild_rescans_even_with_valid_map(void) {
    BoardNandBlockMap stored;

    board_stub_reset_all();
    memset(&stored, 0, sizeof(stored));
    stored.bad[5] = 0xFFU;
    stored.candidate[0] = 0x02U;
    assert(mram_store_save_block_map(1U, &stored) == BOARD_OK);
    board_stub_set_nand_factory_bad_block(1U, 100U);

    assert(nand_map_rebuild(1U) == BOARD_OK);
    assert(board_stub_nand_scan_count(1U) == 1U);

    load_mram_map(1U, 1U, true);
    assert(bit_set(map.bad, 100U));
    assert(!bit_set(map.bad, 40U));
    assert(!bit_set(map.candidate, 1U));
}

static void erase_merges_candidates_before_save(void) {
    BoardNandBlockMap stored;

    board_stub_reset_all();
    memset(&stored, 0, sizeof(stored));
    stored.candidate[2] = 0x10U;
    assert(mram_store_save_block_map(1U, &stored) == BOARD_OK);
    assert(nand_map_load(1U) == BOARD_OK);

    assert(board_nand_erase_start(1U) == BOARD_OK);
    assert(nand_map_save(1U) == BOARD_OK);

    load_mram_map(1U, 1U, true);
    assert(bit_set(map.bad, 20U));
    assert(!bit_set(map.candidate, 20U));
}

int main(void) {
    missing_mram_map_triggers_scan_and_save();
    valid_mram_map_is_used_without_scan();
    corrupted_copy_is_restored();
    rebuild_rescans_even_with_valid_map();
    erase_merges_candidates_before_save();

    return 0;
}
