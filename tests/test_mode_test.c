#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "actions.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"

static void init_duty_context(SystemContext *ctx) {
    board_stub_reset_all();
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_DUTY;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

/* total_blocks derives from NAND capacity; the host stub reports 256 packets,
 * which is two 128-packet blocks, so a clean CMD_TEST runs the write/read/
 * compare path over both blocks, must finish with an empty Nerr, bump the
 * per-bank test counter, and return to DUTY. */
static void clean_test_completes_and_bumps_counter(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreServiceData service;
    static MramStoreTestResult result;
    uint32_t guard;
    uint32_t i;

    init_duty_context(&ctx);
    system_event_queue_init();

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_TEST;
    event.command.test.bank = NAND_BANK_1;
    event.command.test.power_after_done = POWER_AFTER_DONE_OFF;

    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_TEST);
    assert(ctx.test.total_blocks == 2U);

    for (guard = 0U; (guard < 5000U) && (ctx.state == STATE_TEST); ++guard) {
        algorithm_poll(&ctx);
        algorithm_process_events(&ctx);
    }

    assert(ctx.state == STATE_DUTY);
    assert(ctx.test.result_valid);

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.nand1_test_count == 1U);
    assert(service.nand2_test_count == 0U);

    memset(&result, 0, sizeof(result));
    assert(mram_store_load_test_result(1U, &result) == BOARD_OK);
    for (i = 0U; i < TEST_MODE_BLOCK_COUNT; ++i) {
        assert(result.nerr[i] == 0U);
    }
}

/* Nerr is persisted as 24-bit per block; values above 0xFFFFFF saturate. */
static void nerr_saturates_to_24_bit(void) {
    static MramStoreTestResult in;
    static MramStoreTestResult out;

    memset(&in, 0, sizeof(in));
    in.bank = 2U;
    in.nerr[0] = 0x00FFFFFFUL;      /* max representable */
    in.nerr[1] = 0x01234567UL;      /* over 24 bits -> saturates */
    in.nerr[2] = 0x00ABCDEFUL;      /* mid value, round-trips */
    in.nerr[TEST_MODE_BLOCK_COUNT - 1U] = 0x00000001UL;

    assert(mram_store_save_test_result(&in) == BOARD_OK);

    memset(&out, 0, sizeof(out));
    assert(mram_store_load_test_result(2U, &out) == BOARD_OK);

    assert(out.nerr[0] == 0x00FFFFFFUL);
    assert(out.nerr[1] == 0x00FFFFFFUL);
    assert(out.nerr[2] == 0x00ABCDEFUL);
    assert(out.nerr[TEST_MODE_BLOCK_COUNT - 1U] == 0x00000001UL);
}

static void clean_test_resets_progress_and_rebuilds_map(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreServiceData service;
    BoardNandBlockMap map;
    uint8_t is_valid = 0U;
    uint32_t guard;

    board_stub_reset_all();
    init_duty_context(&ctx);
    system_event_queue_init();

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    service.nand2_packet_count = 99U;
    service.nand2_last_packet_crc = 0x4321U;
    service.nand2_last_dumped_packet = 5U;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
    board_stub_set_nand_factory_bad_block(2U, 1500U);

    memset(&event, 0, sizeof(event));
    event.type = EVENT_CMD_TEST;
    event.command.test.bank = NAND_BANK_2;
    event.command.test.power_after_done = POWER_AFTER_DONE_OFF;
    assert(system_event_queue_push_back(&event));
    algorithm_process_events(&ctx);
    assert(ctx.state == STATE_TEST);
    assert(board_stub_nand_scan_count(2U) == 1U);

    for (guard = 0U; (guard < 5000U) && (ctx.state == STATE_TEST); ++guard) {
        algorithm_poll(&ctx);
        algorithm_process_events(&ctx);
    }
    assert(ctx.state == STATE_DUTY);

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.nand2_packet_count == 0U);
    assert(service.nand2_last_packet_crc == 0U);
    assert(service.nand2_last_dumped_packet == 0U);

    assert(board_mram_read_block_map(2U, 2U, &map, &is_valid) == BOARD_OK);
    assert(is_valid != 0U);
    assert(((map.bad[1500U / 8U] >> (1500U % 8U)) & 1U) != 0U);
}

int main(void) {
    clean_test_resets_progress_and_rebuilds_map();
    clean_test_completes_and_bumps_counter();
    nerr_saturates_to_24_bit();

    return 0;
}
