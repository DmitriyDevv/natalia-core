#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "unican.h"
#include "unican_node_addr.h"

#ifdef NATALIA_ENABLE_DETECTOR_PROTO_LOG
#include "detector_log.h"
#endif

#define TRANSPORT_KU_MAX_MSG_ID             (0x000CU)

#define TRANSPORT_KT_MIN_MSG_ID             (0x0100U)
#define TRANSPORT_KT_MAX_MSG_ID             (0x0103U)

#define TRANSPORT_KU_SET_TIME_MSG_ID        (0x0002U)

#define TRANSPORT_TS_ACK_MSG_ID             (0x0201U)

#define TRANSPORT_SHORT_PAYLOAD_SIZE        (6U)
#define TRANSPORT_FILL_BYTE                 (0xAAU)

#define TRANSPORT_KU_STATUS_REQ_MSG_ID      (0x0001U)

#define TRANSPORT_TS_STATUS_MSG_ID          (0x0200U)
#define TRANSPORT_TS_ACK_MSG_ID             (0x0201U)

#define TRANSPORT_SHORT_PAYLOAD_SIZE        (6U)
#define TRANSPORT_TX_QUEUE_LENGTH           (4U)
#define TRANSPORT_TX_MAX_RETRIES            (3U)

static uint8_t transport_rx_buffer[UNICAN_MAX_MESSAGE_DATA];

typedef struct {
    uint16_t message_id;
    uint16_t length;
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];
    uint8_t retries_done;
} TransportTxItem;

static TransportTxItem transport_tx_queue[TRANSPORT_TX_QUEUE_LENGTH];
static uint16_t transport_tx_head = 0U;
static uint16_t transport_tx_tail = 0U;
static uint16_t transport_tx_count = 0U;

static bool transport_tx_active = false;
static uint32_t transport_tx_ok_snapshot = 0U;
static uint32_t transport_tx_failed_snapshot = 0U;

static uint16_t transport_next_tx_index(uint16_t index) {
    ++index;

    if (index >= TRANSPORT_TX_QUEUE_LENGTH) {
        index = 0U;
    }

    return index;
}

static BoardStatus transport_enqueue_short_message(uint16_t message_id,
                                                   const uint8_t* payload) {
    TransportTxItem* item;

    if (payload == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (transport_tx_count >= TRANSPORT_TX_QUEUE_LENGTH) {
        return BOARD_ERR_BUSY;
    }

    item = &transport_tx_queue[transport_tx_head];

    item->message_id = message_id;
    item->length = TRANSPORT_SHORT_PAYLOAD_SIZE;
    item->retries_done = 0U;

    memcpy(item->payload, payload, TRANSPORT_SHORT_PAYLOAD_SIZE);

    transport_tx_head = transport_next_tx_index(transport_tx_head);
    ++transport_tx_count;

    return BOARD_OK;
}

static void transport_drop_front_tx_message(void) {
    TransportTxItem* item;

    if (transport_tx_count == 0U) {
        return;
    }

    item = &transport_tx_queue[transport_tx_tail];

    item->message_id = 0U;
    item->length = 0U;
    item->retries_done = 0U;

    memset(item->payload, 0, sizeof(item->payload));

    transport_tx_tail = transport_next_tx_index(transport_tx_tail);
    --transport_tx_count;
}

static BoardStatus transport_service_tx(void) {
    UnicanStatus protocol_status;
    UnicanMessage message;
    TransportTxItem* item;
    BoardStatus status;

    unican_get_status(&protocol_status);

    if (transport_tx_active) {

        if (protocol_status.tx_busy) {
            return BOARD_OK;
        }

        if (protocol_status.tx_messages_ok > transport_tx_ok_snapshot) {
// #ifdef NATALIA_ENABLE_DETECTOR_PROTO_LOG
//             {
//                 const TransportTxItem* sent_item =
//                     &transport_tx_queue[transport_tx_tail];
//
//                 const UnicanMessage sent_message = {
//                     .message_id = sent_item->message_id,
//                     .address_from = UNICAN_NA_ADDRESS,
//                     .address_to = UNICAN_BVS_ADDRESS,
//                     .length = sent_item->length,
//                     .data = sent_item->payload
//                 };
//
//                 detector_log_tx(&sent_message);
//             }
// #endif
            transport_drop_front_tx_message();
            transport_tx_active = false;
        } else if (protocol_status.tx_messages_failed >
            transport_tx_failed_snapshot) {

            transport_tx_active = false;

            if (transport_tx_count == 0U) {
                return BOARD_ERR_IO;
            }

            item = &transport_tx_queue[transport_tx_tail];

            if (item->retries_done < TRANSPORT_TX_MAX_RETRIES) {
                ++item->retries_done;


            } else {

                transport_drop_front_tx_message();
                return BOARD_ERR_IO;
            }
        } else {

            return BOARD_OK;
        }
    }

    if (transport_tx_count == 0U) {
        return BOARD_OK;
    }

    unican_get_status(&protocol_status);

    if (protocol_status.tx_busy) {
        return BOARD_OK;
    }

    item = &transport_tx_queue[transport_tx_tail];

    message.message_id = item->message_id;
    message.address_from = UNICAN_NA_ADDRESS;
    message.address_to = UNICAN_BVS_ADDRESS;
    message.length = item->length;
    message.data = item->payload;

    transport_tx_ok_snapshot = protocol_status.tx_messages_ok;
    transport_tx_failed_snapshot = protocol_status.tx_messages_failed;

    status = unican_send(&message);

    if (status == BOARD_ERR_BUSY) {
        return BOARD_OK;
    }

    if (status != BOARD_OK) {
        return status;
    }

    transport_tx_active = true;

    return BOARD_OK;
}

static uint16_t transport_read_le_u16(const uint8_t* data) {
    return (uint16_t)data[0] |
        ((uint16_t)data[1] << 8U);
}

static uint32_t transport_read_le_u32(const uint8_t* data) {
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) |
        ((uint32_t)data[3] << 24U);
}

static bool transport_is_command_id(uint16_t message_id) {
    return message_id <= TRANSPORT_KU_MAX_MSG_ID;
}

static bool transport_is_telemetry_command_id(uint16_t message_id) {
    return (message_id >= TRANSPORT_KT_MIN_MSG_ID) &&
        (message_id <= TRANSPORT_KT_MAX_MSG_ID);
}

BoardStatus transport_send_ack(uint16_t command_id,
                               TransportAckStatus status) {
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];

    payload[0] = (uint8_t)(command_id & 0xFFU);
    payload[1] = (uint8_t)(command_id >> 8U);
    payload[2] = (uint8_t)status;
    payload[3] = TRANSPORT_FILL_BYTE;
    payload[4] = TRANSPORT_FILL_BYTE;
    payload[5] = TRANSPORT_FILL_BYTE;

    return transport_enqueue_short_message(TRANSPORT_TS_ACK_MSG_ID,
                                           payload);
}

static uint8_t transport_encode_previous_state(SystemState state) {
    switch (state) {
    case STATE_INIT:
        return 0U;

    case STATE_DUTY:
        return 1U;

    case STATE_TEST:
        return 2U;

    case STATE_ERASE:
        return 3U;

    case STATE_OBSERVE:
        return 4U;

    case STATE_DUMP:
        return 5U;

    case STATE_ALARM:
        return 6U;

    case STATE_SHUTDOWN:
    default:

        return 0U;
    }
}

static uint8_t transport_encode_current_state(SystemState state) {
    switch (state) {
    case STATE_DUTY:
        return 1U;

    case STATE_TEST:
        return 2U;

    case STATE_ERASE:
        return 3U;

    case STATE_OBSERVE:
        return 4U;

    case STATE_DUMP:
        return 5U;

    case STATE_ALARM:
        return 6U;

    case STATE_SHUTDOWN:
        return 7U;

    case STATE_INIT:
    default:
        return 0U;
    }
}

static uint8_t transport_build_mode_byte(const SystemContext* ctx) {
    const uint8_t previous =
        transport_encode_previous_state(ctx->previous_state);
    const uint8_t current =
        transport_encode_current_state(ctx->state);

    return (uint8_t)((previous & 0x07U) |
        ((current & 0x07U) << 3U));
}

static uint8_t transport_build_nand_full_byte(const SystemContext* ctx) {
    uint8_t value = 0U;

    if (ctx->nand1.is_full) {
        value |= (1U << 0U);
    }

    if (ctx->nand2.is_full) {
        value |= (1U << 1U);
    }

    return value;
}

BoardStatus transport_send_status(const SystemContext* ctx) {
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];
    uint32_t board_status_word = 0U;
    uint16_t masked_alarm;
    BoardStatus status;

    if (ctx == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_read_power_status(&board_status_word);
    if (status != BOARD_OK) {
        return status;
    }

    masked_alarm = (uint16_t)(ctx->masked_alarm & 0xFFFFU);

    payload[0] = transport_build_mode_byte(ctx);
    payload[1] = transport_build_nand_full_byte(ctx);

    payload[2] = (uint8_t)(masked_alarm & 0x00FFU);
    payload[3] = (uint8_t)(masked_alarm >> 8U);

    payload[4] = (uint8_t)(board_status_word & 0x00FFUL);
    payload[5] = (uint8_t)((board_status_word >> 8U) & 0x00FFUL);

    return transport_enqueue_short_message(TRANSPORT_TS_STATUS_MSG_ID,
                                           payload);
}

BoardStatus transport_send_telemetry(void) {

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus transport_send_test_result(void) {

    return BOARD_ERR_UNSUPPORTED;
}

static bool transport_payload_is_fill(const UnicanMessage* message) {
    uint16_t index;

    if ((message == NULL) ||
        (message->data == NULL) ||
        (message->length != TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return false;
    }

    for (index = 0U; index < message->length; ++index) {
        if (message->data[index] != TRANSPORT_FILL_BYTE) {
            return false;
        }
    }

    return true;
}

static BoardStatus transport_build_status_request_event(
    const UnicanMessage* message,
    SystemEvent* event) {
    if ((message == NULL) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill(message)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_STATUS_REQ;
    event->msg_id = message->message_id;

    return BOARD_OK;
}

static BoardStatus transport_build_set_time_event(const UnicanMessage* message,
                                                  SystemEvent* event) {
    uint16_t milliseconds;

    if ((message == NULL) ||
        (event == NULL) ||
        (message->data == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->length != TRANSPORT_SHORT_PAYLOAD_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    milliseconds = transport_read_le_u16(&message->data[0]);

    if (milliseconds >= 1000U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_SET_TIME;
    event->msg_id = message->message_id;

    event->command.set_time.time.milliseconds = milliseconds;
    event->command.set_time.time.seconds =
        transport_read_le_u32(&message->data[2]);

    return BOARD_OK;
}

static void transport_handle_message(SystemContext* ctx,
                                     const UnicanMessage* message) {
    BoardStatus status;
    SystemEvent event;

    if ((ctx == NULL) || (message == NULL)) {
        return;
    }

    if ((message->address_from != UNICAN_BVS_ADDRESS) ||
        (message->address_to != UNICAN_NA_ADDRESS)) {
        return;
    }

    if (message->message_id == TRANSPORT_KU_STATUS_REQ_MSG_ID) {
        status = transport_build_status_request_event(message, &event);

        if (status != BOARD_OK) {
            (void)transport_send_ack(message->message_id,
                                     TRANSPORT_ACK_ERR_CONTENT);
            return;
        }

        (void)handle_event(ctx, &event);
        return;
    }

    if (message->message_id == TRANSPORT_KU_SET_TIME_MSG_ID) {
        status = transport_build_set_time_event(message, &event);

        if (status != BOARD_OK) {
            (void)transport_send_ack(message->message_id,
                                     TRANSPORT_ACK_ERR_CONTENT);
            return;
        }

        (void)handle_event(ctx, &event);
        return;
    }

    if (transport_is_command_id(message->message_id)) {

        (void)transport_send_ack(message->message_id,
                                 TRANSPORT_ACK_ERR_OTHER);
        return;
    }

    if (transport_is_telemetry_command_id(message->message_id)) {

        return;
    }


}

BoardStatus transport_poll(SystemContext* ctx, uint32_t now_ms) {
    BoardStatus status;
    BoardStatus tx_status;
    UnicanMessage message;

    if (ctx == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    unican_poll(now_ms);

    tx_status = transport_service_tx();

    while (true) {
        status = unican_receive(&message,
                                transport_rx_buffer,
                                (uint16_t)sizeof(transport_rx_buffer));

        if (status == BOARD_ERR_NOT_READY) {

            status = transport_service_tx();

            if (status != BOARD_OK) {
                return status;
            }

            return tx_status;
        }

        if (status != BOARD_OK) {
            return status;
        }

        // #ifdef NATALIA_ENABLE_DETECTOR_PROTO_LOG
        //         detector_log_rx(&message);
        // #endif
        transport_handle_message(ctx, &message);
    }
}
