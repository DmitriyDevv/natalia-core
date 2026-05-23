#ifndef NATALIA_CORE_TRANSPORT_H
#define NATALIA_CORE_TRANSPORT_H

#include <stdint.h>

#include "state.h"
#include "status.h"
#include "board_api.h"

typedef enum {
    TRANSPORT_ACK_OK          = 0x00U,
    TRANSPORT_ACK_ERR_MSG_ID  = 0x03U,
    TRANSPORT_ACK_ERR_CONTENT = 0x05U,
    TRANSPORT_ACK_ERR_MODE    = 0x07U,
    TRANSPORT_ACK_ERR_OTHER   = 0x09U
} TransportAckStatus;

BoardStatus transport_poll(SystemContext *ctx, uint32_t now_ms);

BoardStatus transport_send_ack(uint16_t command_id, TransportAckStatus status);

BoardStatus transport_send_status(const SystemContext *ctx);
BoardStatus transport_send_telemetry(void);
BoardStatus transport_send_test_result(void);

#endif /* NATALIA_CORE_TRANSPORT_H */