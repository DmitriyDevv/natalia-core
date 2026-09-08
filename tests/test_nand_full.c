#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "board_api.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

#define TS_STATUS_MSG_ID  (0x0D00U)
#define STATUS_SIZE        (6U)
#define FLUSH_POLLS        (4U)

static void begin_observe_test(SystemContext* ctx, NandBank bank,
                               PowerAfterDone power_after_done) {
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_OBSERVE;
    ctx->previous_state = STATE_OBSERVE;
    ctx->nand1.bank = NAND_BANK_1;
    ctx->nand2.bank = NAND_BANK_2;
    ctx->observe.bank = bank;
    ctx->observe.power_after_done = power_after_done;
    ctx->observe.stage = OBSERVE_STAGE_ACTIVE;
    ctx->observe.registration_enabled = true;
    ctx->ped.is_powered = true;

    if (bank == NAND_BANK_1) {
        ctx->nand1.is_powered = true;
        ctx->nand1.is_connected = true;
    } else {
        ctx->nand2.is_powered = true;
        ctx->nand2.is_connected = true;
    }

    board_stub_reset_all();
    assert(board_nand_power_on((uint8_t)bank) == BOARD_OK);
    assert(board_nand_connect((uint8_t)bank) == BOARD_OK);
    system_event_queue_init();
    transport_reset();
}

static void flush_transmit(SystemContext* ctx) {
    uint32_t index;

    for (index = 0U; index < FLUSH_POLLS; ++index) {
        assert(transport_poll(ctx, 0U) == BOARD_OK);
    }
}

static void nand_full_ends_observe_persists_flag_and_reports_bank(void) {
    SystemContext ctx;
    SystemContext rebooted;
    SystemEvent event;
    MramStoreServiceData service;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint16_t length = 0U;
    uint8_t status[STATUS_SIZE];

    begin_observe_test(&ctx, NAND_BANK_2, POWER_AFTER_DONE_OFF);
    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_NAND_FULL;

    assert(handle_event(&ctx, &event) == STATE_DUTY);
    assert(ctx.previous_state == STATE_OBSERVE);
    assert(ctx.observe.stage == OBSERVE_STAGE_EXIT_FULL);
    assert(ctx.nand2.is_full);
    assert(!ctx.nand2.is_connected);
    assert(!ctx.nand2.is_powered);

    (void)memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);
    assert(service.nand1_full == 0U);
    assert(service.nand2_full == 1U);

    flush_transmit(&ctx);
    assert(board_comm_stub_tx_count() == 1U);
    assert(board_comm_stub_last_tx(&message_id, &address_to, status,
                                   sizeof(status), &length));
    assert(message_id == TS_STATUS_MSG_ID);
    assert(length == STATUS_SIZE);
    assert(status[1] == 0x02U);

    (void)memset(&rebooted, 0, sizeof(rebooted));
    assert(action_load_mram(&rebooted) == ACTION_OK);
    assert(!rebooted.nand1.is_full);
    assert(rebooted.nand2.is_full);
}

static void failed_full_completion_enters_alarm(void) {
    SystemContext ctx;
    SystemEvent event;

    begin_observe_test(&ctx, NAND_BANK_1, POWER_AFTER_DONE_OFF);
    board_stub_set_mram_write_fail(true);
    (void)memset(&event, 0, sizeof(event));
    event.type = EVENT_NAND_FULL;

    assert(handle_event(&ctx, &event) == STATE_ALARM);
    assert(ctx.previous_state == STATE_OBSERVE);
    assert(ctx.observe.stage == OBSERVE_STAGE_EXIT_ALARM);
    assert(ctx.nand1.is_full);
    assert(!ctx.ped.is_powered);
    assert(!ctx.nand1.is_powered);
    assert(!ctx.nand2.is_powered);

    board_stub_set_mram_write_fail(false);
}

int main(void) {
    nand_full_ends_observe_persists_flag_and_reports_bank();
    failed_full_completion_enters_alarm();

    return 0;
}
