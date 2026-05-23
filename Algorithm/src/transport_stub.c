#include "transport.h"

BoardStatus transport_poll(SystemContext* ctx, uint32_t now_ms) {
    (void)ctx;
    (void)now_ms;
    return BOARD_OK;
}

BoardStatus transport_send_ack(uint16_t command_id,
                               TransportAckStatus status) {
    (void)command_id;
    (void)status;
    return BOARD_OK;
}

BoardStatus transport_send_status(const SystemContext* ctx) {
    (void)ctx;
    return BOARD_OK;
}

BoardStatus transport_send_telemetry(void) {
    return BOARD_OK;
}

BoardStatus transport_send_test_result(void) {
    return BOARD_OK;
}
