#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "board_api.h"
#include "board_stub.h"
#include "mram_store.h"
#include "state.h"

#define MRAM_CONFIG_OFFSET (0U)

static MramStoreConfig make_config(uint16_t marker) {
    MramStoreConfig config;

    (void)memset(&config, 0, sizeof(config));
    config.alarm_mask = ALARM_PU_TEMP;
    config.can_control = marker;
    config.pu_temp_min = (int16_t)(-40 + (int16_t)marker);
    config.pu_temp_max = (int16_t)(70 + (int16_t)marker);
    config.config_version = marker;

    return config;
}

static void write_config_copy(uint8_t copy_id, const MramStoreConfig *config) {
    assert(board_mram_write(copy_id, MRAM_CONFIG_OFFSET, config,
                            sizeof(*config)) == BOARD_OK);
}

static void assert_config_copy(uint8_t copy_id, const MramStoreConfig *expected) {
    MramStoreConfig actual;

    (void)memset(&actual, 0, sizeof(actual));
    assert(board_mram_read(copy_id, MRAM_CONFIG_OFFSET, &actual,
                           sizeof(actual)) == BOARD_OK);
    assert(memcmp(&actual, expected, sizeof(actual)) == 0);
}

static void assert_context_loaded(const SystemContext *ctx,
                                  const MramStoreConfig *expected) {
    assert(ctx->alarm_mask == alarm_sanitize_mask(expected->alarm_mask));
    assert(ctx->can_control == expected->can_control);
    assert(ctx->pu_temp_min == expected->pu_temp_min);
    assert(ctx->pu_temp_max == expected->pu_temp_max);
}

static void first_invalid_loads_second_copy(void) {
    SystemContext ctx;
    MramStoreStatus status;
    MramStoreConfig good = make_config(2U);
    MramStoreConfig bad = make_config(91U);

    board_stub_reset_all();
    write_config_copy(1U, &bad);
    write_config_copy(2U, &good);
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, true);

    (void)memset(&ctx, 0, sizeof(ctx));
    assert(action_load_mram(&ctx) == ACTION_OK);
    assert_context_loaded(&ctx, &good);

    assert(mram_store_check(&status) == BOARD_OK);
    assert(status.copy1_valid != 0U);
    assert(status.copy2_valid != 0U);
    assert_config_copy(1U, &good);
    assert_config_copy(2U, &good);
}

static void second_invalid_loads_first_copy(void) {
    SystemContext ctx;
    MramStoreStatus status;
    MramStoreConfig good = make_config(1U);
    MramStoreConfig bad = make_config(92U);

    board_stub_reset_all();
    write_config_copy(1U, &good);
    write_config_copy(2U, &bad);
    board_stub_set_mram_crc_valid(1U, true);
    board_stub_set_mram_crc_valid(2U, false);

    (void)memset(&ctx, 0, sizeof(ctx));
    assert(action_load_mram(&ctx) == ACTION_OK);
    assert_context_loaded(&ctx, &good);

    assert(mram_store_check(&status) == BOARD_OK);
    assert(status.copy1_valid != 0U);
    assert(status.copy2_valid != 0U);
    assert_config_copy(1U, &good);
    assert_config_copy(2U, &good);
}

static void restore_action_repairs_invalid_copy(void) {
    SystemContext ctx;
    MramStoreStatus status;
    MramStoreConfig good = make_config(1U);
    MramStoreConfig bad = make_config(99U);

    board_stub_reset_all();
    write_config_copy(1U, &good);
    write_config_copy(2U, &bad);
    board_stub_set_mram_crc_valid(1U, true);
    board_stub_set_mram_crc_valid(2U, false);

    (void)memset(&ctx, 0, sizeof(ctx));
    assert(action_restore_mram_copy(&ctx) == ACTION_OK);
    assert(mram_store_check(&status) == BOARD_OK);
    assert(status.copy1_valid != 0U);
    assert(status.copy2_valid != 0U);
    assert_config_copy(1U, &good);
    assert_config_copy(2U, &good);
}

static void both_invalid_raise_active_mram_alarm(void) {
    SystemContext ctx;

    board_stub_reset_all();
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    (void)memset(&ctx, 0, sizeof(ctx));
    assert(action_check_mram(&ctx) == ACTION_ALARM);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert((ctx.masked_alarm & ALARM_MRAM) != 0U);
}

static void boot_with_both_invalid_enters_alarm_without_provisioning(void) {
    SystemContext ctx;
    SystemEvent boot_event;
    MramStoreConfig before_copy1 = make_config(31U);
    MramStoreConfig before_copy2 = make_config(32U);

    board_stub_reset_all();
    write_config_copy(1U, &before_copy1);
    write_config_copy(2U, &before_copy2);
    board_stub_set_mram_crc_valid(1U, false);
    board_stub_set_mram_crc_valid(2U, false);

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_INIT;
    (void)memset(&boot_event, 0, sizeof(boot_event));
    boot_event.type = EVENT_BOOT;

    assert(handle_event(&ctx, &boot_event) == STATE_ALARM);
    assert(ctx.state == STATE_ALARM);
    assert(ctx.previous_state == STATE_INIT);
    assert((ctx.alarm_status & ALARM_MRAM) != 0U);
    assert((ctx.masked_alarm & ALARM_MRAM) != 0U);
    assert_config_copy(1U, &before_copy1);
    assert_config_copy(2U, &before_copy2);
}

int main(void) {
    first_invalid_loads_second_copy();
    second_invalid_loads_first_copy();
    restore_action_repairs_invalid_copy();
    both_invalid_raise_active_mram_alarm();
    boot_with_both_invalid_enters_alarm_without_provisioning();

    return 0;
}
