#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_comm_stub.h"
#include "event_queue.h"
#include "state.h"
#include "transport.h"

/* Full command admissibility matrix from CAN protocol v2.1 table 5.2.10.
 * Columns are the seven operating modes; a rejected command must produce a
 * TS "Квитанция" with error code 3 ("недопустимая КУ в текущем режиме"). */

#define TS_ACK_MSG_ID   (0x0D01U)
#define ACK_ERR_MODE    (0x07U)
#define ADDR_NA         (0x1EU)
#define ADDR_BVS        (0x05U)

#define MODE_COUNT      (7U)

static const SystemState kModes[MODE_COUNT] = {
    STATE_DUTY,     /* mode 1 */
    STATE_ERASE,    /* mode 2 */
    STATE_TEST,     /* mode 3 */
    STATE_OBSERVE,  /* mode 4 */
    STATE_DUMP,     /* mode 5 */
    STATE_ALARM,    /* mode 6 */
    STATE_SHUTDOWN  /* mode 7 */
};

typedef struct {
    EventType type;
    /* allowed[i] == true when the command is admissible in kModes[i] */
    bool allowed[MODE_COUNT];
} AdmissibilityRow;

/* Command events that reach the FSM. Destination/Device ID (КУ 15/16) are
 * handled in transport and never enter handle_event, so they are not listed.
 * КУ 14 (Sputniks set-time) maps onto EVENT_CMD_SET_TIME. */
static const AdmissibilityRow kMatrix[] = {
    /*                      D  E  T  O  P  A  S  */
    { EVENT_CMD_TELEM_REQ,  { 1, 0, 0, 1, 0, 1, 0 } }, /* КУ 1  */
    { EVENT_CMD_STATUS_REQ, { 1, 1, 1, 1, 1, 1, 1 } }, /* КУ 2  */
    { EVENT_CMD_SET_TIME,   { 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 3/14 */
    { EVENT_CMD_OBSERVE_START, { 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 4 */
    { EVENT_CMD_OBSERVE_CTRL,  { 0, 0, 0, 1, 0, 0, 0 } }, /* КУ 5 */
    { EVENT_CMD_DUTY,       { 1, 1, 1, 1, 1, 0, 0 } }, /* КУ 6  */
    { EVENT_CMD_DUMP,       { 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 7  */
    { EVENT_CMD_SET_CFG,    { 1, 0, 0, 0, 0, 1, 0 } }, /* КУ 8  */
    { EVENT_CMD_ERASE,      { 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 9  */
    { EVENT_CMD_TEST,       { 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 10 */
    { EVENT_CMD_TEST_RESULT,{ 1, 0, 0, 0, 0, 0, 0 } }, /* КУ 11 */
    { EVENT_CMD_SHUTDOWN,   { 1, 1, 1, 1, 1, 1, 0 } }, /* КУ 12 */
    { EVENT_CMD_RESET_ALARM,{ 0, 0, 0, 0, 0, 1, 0 } }, /* КУ 13 */
    { EVENT_CMD_VERSION_REQ,{ 1, 1, 1, 1, 1, 1, 1 } }  /* КУ 17 */
};

static void prepare(SystemContext* ctx, SystemState mode, SystemEvent* event,
                    EventType type) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = mode;
    system_event_queue_init();
    board_comm_stub_reset();
    transport_reset();

    memset(event, 0, sizeof(*event));
    event->type = type;
    event->msg_id = 0x0F00U; /* echoed into the quittance */
}

/* A forbidden command must not transition and must emit an ERR_MODE quittance. */
static void expect_rejected(SystemState mode, EventType type) {
    SystemContext ctx;
    SystemEvent event;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint8_t got[6];
    uint16_t length = 0U;

    prepare(&ctx, mode, &event, type);

    assert(handle_event(&ctx, &event) == mode);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got),
                                   &length));
    assert(message_id == TS_ACK_MSG_ID);
    assert(got[2] == ACK_ERR_MODE);
}

/* An admissible command must never answer with a mode-rejection quittance. */
static void expect_not_mode_rejected(SystemState mode, EventType type) {
    SystemContext ctx;
    SystemEvent event;
    uint16_t message_id = 0U;
    uint16_t address_to = 0U;
    uint8_t got[6];
    uint16_t length = 0U;
    int i;

    prepare(&ctx, mode, &event, type);

    (void)handle_event(&ctx, &event);

    for (i = 0; i < 4; ++i) {
        assert(transport_poll(&ctx, 0U) == BOARD_OK);
    }

    if (board_comm_stub_last_tx(&message_id, &address_to, got, sizeof(got),
                                &length)) {
        assert(!((message_id == TS_ACK_MSG_ID) && (got[2] == ACK_ERR_MODE)));
    }
}

static void full_matrix_matches_table_5_2_10(void) {
    size_t row;
    uint32_t m;

    for (row = 0U; row < (sizeof(kMatrix) / sizeof(kMatrix[0])); ++row) {
        for (m = 0U; m < MODE_COUNT; ++m) {
            if (kMatrix[row].allowed[m]) {
                continue;
            }
            expect_rejected(kModes[m], kMatrix[row].type);
        }
    }
}

/* Positive direction for the two commands admissible in every mode. */
static void status_and_version_never_mode_rejected(void) {
    uint32_t m;

    for (m = 0U; m < MODE_COUNT; ++m) {
        expect_not_mode_rejected(kModes[m], EVENT_CMD_STATUS_REQ);
        expect_not_mode_rejected(kModes[m], EVENT_CMD_VERSION_REQ);
    }
}

int main(void) {
    full_matrix_matches_table_5_2_10();
    status_and_version_never_mode_rejected();

    return 0;
}
