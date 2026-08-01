#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "actions.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"

static void init_duty_context(SystemContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_DUTY;
    ctx->previous_state = STATE_DUTY;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->test.failed_address = TEST_MODE_FAILED_ADDRESS_NONE;
}

/* total_blocks derives from NAND capacity; the host stub reports 64 packets,
 * which is below one 128-packet block, so a clean CMD_TEST must still run to a
 * штатное completion (empty Nerr), bump the per-bank test counter, and return
 * to DUTY. Exercises the FSM wiring + counter increment end to end. */
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
    assert(ctx.test.total_blocks == 0U);

    for (guard = 0U; (guard < 1000U) && (ctx.state == STATE_TEST); ++guard) {
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

int main(void) {
    clean_test_completes_and_bumps_counter();
    nerr_saturates_to_24_bit();

    return 0;
}
