#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board_comm_stub.h"
#include "event_queue.h"
#include "state.h"
#include "transport.h"

#define KU_STATUS_REQ_MSG_ID (0x0001U)
#define SHORT_PAYLOAD_SIZE   (6U)
#define FILL_BYTE            (0xAAU)
#define ADDR_NA              (0x1EU)
#define ADDR_BVS             (0x05U)
#define ADDR_OTHER           (0x03U)

static void make_fill_payload(uint8_t* payload) {
    memset(payload, FILL_BYTE, SHORT_PAYLOAD_SIZE);
}

static void known_command_is_parsed_and_enqueued(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    system_event_queue_init();
    board_comm_stub_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);

    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_STATUS_REQ);
    assert(event.msg_id == KU_STATUS_REQ_MSG_ID);
}

static void message_for_other_node_is_dropped(void) {
    SystemContext ctx;
    uint8_t payload[SHORT_PAYLOAD_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    system_event_queue_init();
    board_comm_stub_reset();
    make_fill_payload(payload);

    board_comm_stub_inject_rx(KU_STATUS_REQ_MSG_ID, ADDR_BVS, ADDR_OTHER,
                              payload, SHORT_PAYLOAD_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

int main(void) {
    known_command_is_parsed_and_enqueued();
    message_for_other_node_is_dropped();

    return 0;
}
