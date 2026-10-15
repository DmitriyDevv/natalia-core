#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

/* T-05: the two MRAM copies. A copy that fails its CRC is restored from the good
 * one before any read, so the configuration always comes from a valid copy. With
 * both copies invalid the board raises ALARM_MRAM and does not write a factory
 * image by itself: only KU 8 initialises the MRAM. */

static void begin(void) {
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();
}

static MramStoreConfig config_with(int16_t marker) {
    MramStoreConfig config;

    memset(&config, 0, sizeof(config));
    config.pu_temp_max = marker;
    config.alarm_mask = (uint16_t)(ALARM_ALL_MASK & 0xFFFFU);
    return config;
}

static int16_t raw_marker(uint8_t copy_id) {
    MramStoreConfig config;

    assert(board_mram_read(copy_id, 0U, &config, sizeof(config)) == BOARD_OK);
    return config.pu_temp_max;
}

static void store_and_diverge(uint8_t bad_copy) {
    MramStoreConfig good = config_with(111);
    MramStoreConfig bad = config_with(222);

    assert(mram_store_save_config(&good) == BOARD_OK);
    assert(board_mram_write(bad_copy, 0U, &bad, sizeof(bad)) == BOARD_OK);
    board_stub_set_mram_crc_valid(bad_copy, false);
}

static void invalid_copy1_is_read_from_copy2(void) {
    MramStoreConfig loaded;

    begin();
    store_and_diverge(1U);

    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.pu_temp_max == 111);
}

static void invalid_copy2_is_read_from_copy1(void) {
    MramStoreConfig loaded;

    begin();
    store_and_diverge(2U);

    assert(mram_store_load_config(&loaded) == BOARD_OK);
    assert(loaded.pu_temp_max == 111);
}

static void restore_makes_both_copies_valid_and_equal(void) {
    SystemContext ctx;
    MramStoreStatus status;

    begin();
    memset(&ctx, 0, sizeof(ctx));
    store_and_diverge(2U);

    assert(action_restore_mram_copy(&ctx) == ACTION_OK);
    assert(mram_store_check(&status) == BOARD_OK);
    assert((status.copy1_valid != 0U) && (status.copy2_valid != 0U));
    assert(raw_marker(1U) == 111);
    assert(raw_marker(2U) == 111);
}

static void both_invalid_raises_mram(void) {
    SystemContext ctx;

    begin();
    memset(&ctx, 0, sizeof(ctx));
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    assert(action_check_mram(&ctx) == ACTION_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert(ctx.masked_alarm != 0U);
}

static void boot_with_both_invalid_enters_alarm_without_factory_image(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreStatus status;

    begin();
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_INIT;
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_BOOT;
    (void)handle_event(&ctx, &event);

    assert(ctx.state == STATE_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert(!ctx.thresholds_loaded);
    assert(mram_store_check(&status) == BOARD_OK);
    assert((status.copy1_valid == 0U) && (status.copy2_valid == 0U));
}

int main(void) {
    invalid_copy1_is_read_from_copy2();
    invalid_copy2_is_read_from_copy1();
    restore_makes_both_copies_valid_and_equal();
    both_invalid_raises_mram();
    boot_with_both_invalid_enters_alarm_without_factory_image();

    return 0;
}
