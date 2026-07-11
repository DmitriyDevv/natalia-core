#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "event_queue.h"
#include "unican.h"
#include "unican_node_addr.h"

#ifdef NATALIA_ENABLE_DETECTOR_PROTO_LOG
#include "detector_log.h"
#endif

#define TRANSPORT_KU_TELEM_REQ_MSG_ID              (0x0000U)
#define TRANSPORT_KU_STATUS_REQ_MSG_ID             (0x0001U)
#define TRANSPORT_KU_SET_TIME_MSG_ID               (0x0002U)
#define TRANSPORT_KU_OBSERVE_START_MSG_ID          (0x0003U)
#define TRANSPORT_KU_OBSERVE_CTRL_MSG_ID           (0x0004U)
#define TRANSPORT_KU_DUTY_MSG_ID                   (0x0005U)
#define TRANSPORT_KU_DUMP_MSG_ID                   (0x0006U)
#define TRANSPORT_KU_SET_CFG_MSG_ID                (0x0007U)
#define TRANSPORT_KU_ERASE_MSG_ID                  (0x0008U)
#define TRANSPORT_KU_TEST_MSG_ID                   (0x0009U)
#define TRANSPORT_KU_TEST_RESULT_MSG_ID            (0x000AU)
#define TRANSPORT_KU_SHUTDOWN_MSG_ID               (0x000BU)
#define TRANSPORT_KU_RESET_ALARM_MSG_ID            (0x000CU)
#define TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID      (0x0401U)
#define TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID     (0x0A61U)
#define TRANSPORT_KU_SET_DEVICE_ID_MSG_ID          (0x0A62U)

#define TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_MSG_ID (0xF210U)
#define TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_MSG_ID   (0xF221U)
#define TRANSPORT_KT_SP_MCLWAIN_MSG_ID             (0x0100U)

#define TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_SIZE   (125U)
#define TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_SIZE     (76U)
#define TRANSPORT_KT_SP_MCLWAIN_SIZE               (24U)

#define TRANSPORT_TS_STATUS_MSG_ID                 (0x0200U)
#define TRANSPORT_TS_ACK_MSG_ID                    (0x0201U)

#define TRANSPORT_SHORT_PAYLOAD_SIZE               (6U)
#define TRANSPORT_SET_CFG_PAYLOAD_SIZE             (66U)
#define TRANSPORT_FILL_BYTE                        (0xAAU)
#define TRANSPORT_TX_QUEUE_LENGTH                  (4U)
#define TRANSPORT_TX_MAX_RETRIES                   (3U)

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

static uint16_t transport_remote_address = UNICAN_BVS_ADDRESS;
static uint16_t transport_local_address = UNICAN_NA_ADDRESS;

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
            transport_drop_front_tx_message();
            transport_tx_active = false;
        } else if (protocol_status.tx_messages_failed > transport_tx_failed_snapshot) {
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
    message.address_from = transport_local_address;
    message.address_to = transport_remote_address;
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

static uint32_t transport_read_le_u24(const uint8_t* data) {
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U);
}

static uint32_t transport_read_le_u32(const uint8_t* data) {
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) |
        ((uint32_t)data[3] << 24U);
}

static bool transport_is_valid_bank(uint8_t bank_id) {
    return (bank_id == 1U) || (bank_id == 2U);
}

static NandBank transport_decode_bank(uint8_t bank_id) {
    if (bank_id == 1U) {
        return NAND_BANK_1;
    }

    if (bank_id == 2U) {
        return NAND_BANK_2;
    }

    return NAND_BANK_NONE;
}

static PowerAfterDone transport_decode_power_after_done(uint8_t bit_value) {
    if (bit_value != 0U) {
        return POWER_AFTER_DONE_KEEP;
    }

    return POWER_AFTER_DONE_OFF;
}

static bool transport_is_known_command_id(uint16_t message_id) {
    switch (message_id) {
    case TRANSPORT_KU_TELEM_REQ_MSG_ID:
    case TRANSPORT_KU_STATUS_REQ_MSG_ID:
    case TRANSPORT_KU_SET_TIME_MSG_ID:
    case TRANSPORT_KU_OBSERVE_START_MSG_ID:
    case TRANSPORT_KU_OBSERVE_CTRL_MSG_ID:
    case TRANSPORT_KU_DUTY_MSG_ID:
    case TRANSPORT_KU_DUMP_MSG_ID:
    case TRANSPORT_KU_SET_CFG_MSG_ID:
    case TRANSPORT_KU_ERASE_MSG_ID:
    case TRANSPORT_KU_TEST_MSG_ID:
    case TRANSPORT_KU_TEST_RESULT_MSG_ID:
    case TRANSPORT_KU_SHUTDOWN_MSG_ID:
    case TRANSPORT_KU_RESET_ALARM_MSG_ID:
    case TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID:
    case TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID:
    case TRANSPORT_KU_SET_DEVICE_ID_MSG_ID:
        return true;

    default:
        return false;
    }
}

static bool transport_is_known_telemetry_command_id(uint16_t message_id) {
    switch (message_id) {
    case TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_MSG_ID:
    case TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_MSG_ID:
    case TRANSPORT_KT_SP_MCLWAIN_MSG_ID:
        return true;

    default:
        return false;
    }
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

static bool transport_payload_is_fill_range(const UnicanMessage* message,
                                            uint16_t first,
                                            uint16_t limit) {
    uint16_t index;

    if ((message == NULL) ||
        (message->data == NULL) ||
        (message->length < limit) ||
        (first > limit)) {
        return false;
    }

    for (index = first; index < limit; ++index) {
        if (message->data[index] != TRANSPORT_FILL_BYTE) {
            return false;
        }
    }

    return true;
}

static bool transport_payload_is_fill(const UnicanMessage* message) {
    return transport_payload_is_fill_range(message, 0U, TRANSPORT_SHORT_PAYLOAD_SIZE);
}

static bool transport_is_short_message(const UnicanMessage* message) {
    return (message != NULL) &&
        (message->data != NULL) &&
        (message->length == TRANSPORT_SHORT_PAYLOAD_SIZE);
}

static BoardStatus transport_build_fill_command_event(const UnicanMessage* message,
                                                      SystemEvent* event,
                                                      EventType type) {
    if ((message == NULL) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill(message)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = type;
    event->msg_id = message->message_id;

    return BOARD_OK;
}

static BoardStatus transport_build_set_time_event(const UnicanMessage* message,
                                                  SystemEvent* event) {
    uint16_t milliseconds;

    if (!transport_is_short_message(message) || (event == NULL)) {
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

static BoardStatus transport_build_sputniks_set_time_event(const UnicanMessage* message,
                                                           SystemEvent* event) {
    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_SET_TIME;
    event->msg_id = message->message_id;
    event->command.set_time.time.milliseconds = 0U;
    event->command.set_time.time.seconds =
        transport_read_le_u32(&message->data[0]);

    return BOARD_OK;
}

static bool transport_validate_observe_params(uint16_t params) {
    uint16_t event_mode = params & 0x0007U;
    uint16_t event_count = (params >> 3U) & 0x0007U;
    uint16_t spectrum_mode = (params >> 6U) & 0x0003U;
    uint16_t hist_mode = (params >> 8U) & 0x0007U;

    if (event_mode > 6U) {
        return false;
    }

    if (event_count > 5U) {
        return false;
    }

    if (spectrum_mode > 2U) {
        return false;
    }

    if (hist_mode > 4U) {
        return false;
    }

    if ((params & (1U << 11U)) != 0U) {
        return false;
    }

    if ((spectrum_mode == 0U) && (hist_mode != 0U)) {
        return false;
    }

    if ((spectrum_mode == 2U) && (hist_mode != 0U)) {
        return false;
    }

    return true;
}

static BoardStatus transport_build_observe_start_event(const UnicanMessage* message,
                                                       SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;
    uint16_t observe_params;
    uint16_t trigger_config;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->data[5] != TRANSPORT_FILL_BYTE) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    observe_params = transport_read_le_u16(&message->data[1]);
    trigger_config = transport_read_le_u16(&message->data[3]);

    if (!transport_validate_observe_params(observe_params)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_OBSERVE_START;
    event->msg_id = message->message_id;
    event->command.observe_start.bank = transport_decode_bank(bank_id);
    event->command.observe_start.power_after_done =
        transport_decode_power_after_done((config >> 5U) & 0x01U);
    event->command.observe_start.observe_params = observe_params;
    event->command.observe_start.trigger_config = trigger_config;
    event->command.observe_start.acquisition_period_ticks = observe_params;
    event->command.observe_start.ped_power_enabled =
        (((config >> 2U) & 0x01U) != 0U);
    event->command.observe_start.ped_sleep_enabled =
        (((config >> 3U) & 0x01U) != 0U);
    event->command.observe_start.registration_enabled =
        (((config >> 4U) & 0x01U) != 0U);
    event->command.observe_start.ped_power_after_full =
        (((config >> 6U) & 0x01U) != 0U);
    event->command.observe_start.ped_sleep_after_full =
        (((config >> 7U) & 0x01U) != 0U);

    return BOARD_OK;
}

static BoardStatus transport_build_observe_ctrl_event(const UnicanMessage* message,
                                                      SystemEvent* event) {
    uint8_t config;
    uint16_t observe_params;
    uint16_t trigger_config;
    bool ped_power_enabled;
    bool ped_sleep_enabled;
    bool registration_enabled;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->data[5] != TRANSPORT_FILL_BYTE) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];

    if ((config & 0x03U) != 0x03U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xE0U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    observe_params = transport_read_le_u16(&message->data[1]);
    trigger_config = transport_read_le_u16(&message->data[3]);

    if (!transport_validate_observe_params(observe_params)) {
        return BOARD_ERR_INVALID_ARG;
    }

    ped_power_enabled = (((config >> 2U) & 0x01U) != 0U);
    ped_sleep_enabled = (((config >> 3U) & 0x01U) != 0U);
    registration_enabled = (((config >> 4U) & 0x01U) != 0U);

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_OBSERVE_CTRL;
    event->msg_id = message->message_id;
    event->command.observe_ctrl.ped_power_enabled = ped_power_enabled;
    event->command.observe_ctrl.sleep_enabled = ped_sleep_enabled;
    event->command.observe_ctrl.registration_enabled = registration_enabled;
    event->command.observe_ctrl.inhibit_enabled =
        (!ped_power_enabled || ped_sleep_enabled || !registration_enabled);
    event->command.observe_ctrl.observe_params = observe_params;
    event->command.observe_ctrl.trigger_config = trigger_config;

    return BOARD_OK;
}

static BoardStatus transport_build_duty_event(const UnicanMessage* message,
                                              SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill_range(message, 1U, TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xE0U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_DUTY;
    event->msg_id = message->message_id;
    event->command.duty.bank = transport_decode_bank(bank_id);
    event->command.duty.power_after_done =
        transport_decode_power_after_done((config >> 2U) & 0x01U);
    event->command.duty.ped_power_enabled =
        (((config >> 3U) & 0x01U) != 0U);
    event->command.duty.ped_sleep_enabled =
        (((config >> 4U) & 0x01U) != 0U);

    return BOARD_OK;
}

static BoardStatus transport_build_dump_event(const UnicanMessage* message,
                                              SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;
    uint8_t output_interface;
    uint8_t output_type;
    uint32_t packet_count;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((message->data[4] != TRANSPORT_FILL_BYTE) ||
        (message->data[5] != TRANSPORT_FILL_BYTE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;
    output_interface = (config >> 3U) & 0x01U;
    output_type = (config >> 4U) & 0x01U;
    packet_count = transport_read_le_u24(&message->data[1]);

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xE0U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (output_interface != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((output_type == 0U) && (packet_count == 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (packet_count > (UINT32_MAX / DUMP_MODE_PACKET_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_DUMP;
    event->msg_id = message->message_id;
    event->command.dump.bank = transport_decode_bank(bank_id);
    event->command.dump.power_after_done =
        transport_decode_power_after_done((config >> 2U) & 0x01U);
    event->command.dump.start_address = 0U;
    event->command.dump.requested_packet_count = packet_count;
    event->command.dump.dump_all = (output_type != 0U);

    if (output_type == 0U) {
        event->command.dump.size = packet_count * DUMP_MODE_PACKET_SIZE;
    } else {
        event->command.dump.size = 0U;
    }

    return BOARD_OK;
}

static BoardStatus transport_build_set_cfg_event(const UnicanMessage* message,
                                                 SystemEvent* event) {
    if ((message == NULL) || (event == NULL) || (message->data == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->length != TRANSPORT_SET_CFG_PAYLOAD_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_SET_CFG;
    event->msg_id = message->message_id;
    event->command.set_config.config_id = transport_read_le_u16(&message->data[0]);

    return BOARD_OK;
}

static BoardStatus transport_build_erase_event(const UnicanMessage* message,
                                               SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill_range(message, 1U, TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xF8U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_ERASE;
    event->msg_id = message->message_id;
    event->command.erase.bank = transport_decode_bank(bank_id);
    event->command.erase.power_after_done =
        transport_decode_power_after_done((config >> 2U) & 0x01U);

    return BOARD_OK;
}

static BoardStatus transport_build_test_event(const UnicanMessage* message,
                                              SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill_range(message, 1U, TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xF8U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_TEST;
    event->msg_id = message->message_id;
    event->command.test.bank = transport_decode_bank(bank_id);
    event->command.test.power_after_done =
        transport_decode_power_after_done((config >> 2U) & 0x01U);
    event->command.test.test_mask = 0U;

    return BOARD_OK;
}

static BoardStatus transport_build_test_result_event(const UnicanMessage* message,
                                                     SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill_range(message, 1U, TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xFCU) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_TEST_RESULT;
    event->msg_id = message->message_id;
    event->command.test_result.bank = transport_decode_bank(bank_id);

    return BOARD_OK;
}

static BoardStatus transport_build_command_event(const UnicanMessage* message,
                                                 SystemEvent* event) {
    if ((message == NULL) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    switch (message->message_id) {
    case TRANSPORT_KU_TELEM_REQ_MSG_ID:
        return transport_build_fill_command_event(message,
                                                  event,
                                                  EVENT_CMD_TELEM_REQ);

    case TRANSPORT_KU_STATUS_REQ_MSG_ID:
        return transport_build_fill_command_event(message,
                                                  event,
                                                  EVENT_CMD_STATUS_REQ);

    case TRANSPORT_KU_SET_TIME_MSG_ID:
        return transport_build_set_time_event(message, event);

    case TRANSPORT_KU_OBSERVE_START_MSG_ID:
        return transport_build_observe_start_event(message, event);

    case TRANSPORT_KU_OBSERVE_CTRL_MSG_ID:
        return transport_build_observe_ctrl_event(message, event);

    case TRANSPORT_KU_DUTY_MSG_ID:
        return transport_build_duty_event(message, event);

    case TRANSPORT_KU_DUMP_MSG_ID:
        return transport_build_dump_event(message, event);

    case TRANSPORT_KU_SET_CFG_MSG_ID:
        return transport_build_set_cfg_event(message, event);

    case TRANSPORT_KU_ERASE_MSG_ID:
        return transport_build_erase_event(message, event);

    case TRANSPORT_KU_TEST_MSG_ID:
        return transport_build_test_event(message, event);

    case TRANSPORT_KU_TEST_RESULT_MSG_ID:
        return transport_build_test_result_event(message, event);

    case TRANSPORT_KU_SHUTDOWN_MSG_ID:
        return transport_build_fill_command_event(message,
                                                  event,
                                                  EVENT_CMD_SHUTDOWN);

    case TRANSPORT_KU_RESET_ALARM_MSG_ID:
        return transport_build_fill_command_event(message,
                                                  event,
                                                  EVENT_CMD_RESET_ALARM);

    case TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID:
        return transport_build_sputniks_set_time_event(message, event);

    default:
        return BOARD_ERR_INVALID_ARG;
    }
}

static BoardStatus transport_handle_address_command(const UnicanMessage* message) {
    if (!transport_is_short_message(message)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->message_id == TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID) {
        if (message->data[0] == 0U) {
            return BOARD_ERR_INVALID_ARG;
        }

        transport_remote_address = message->data[0];
        return BOARD_OK;
    }

    if (message->message_id == TRANSPORT_KU_SET_DEVICE_ID_MSG_ID) {
        if (message->data[0] == 0U) {
            return BOARD_ERR_INVALID_ARG;
        }

        transport_local_address = message->data[0];
        return BOARD_OK;
    }

    return BOARD_ERR_INVALID_ARG;
}

static void transport_handle_known_command(SystemContext* ctx,
                                           const UnicanMessage* message) {
    BoardStatus status;
    SystemEvent event;

    if ((message->message_id == TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID) ||
        (message->message_id == TRANSPORT_KU_SET_DEVICE_ID_MSG_ID)) {
        status = transport_handle_address_command(message);

        if (status == BOARD_OK) {
            (void)transport_send_ack(message->message_id, TRANSPORT_ACK_OK);
        } else {
            (void)transport_send_ack(message->message_id, TRANSPORT_ACK_ERR_CONTENT);
        }

        return;
    }

    status = transport_build_command_event(message, &event);

    if (status != BOARD_OK) {
        (void)transport_send_ack(message->message_id,
                                 TRANSPORT_ACK_ERR_CONTENT);
        return;
    }

    (void)ctx;
    (void)system_event_queue_push_back(&event);
}

static void transport_handle_known_telemetry(SystemContext* ctx,
                                             const UnicanMessage* message) {
    SystemEvent event;

    if ((ctx == NULL) || (message == NULL)) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.msg_id = message->message_id;

    switch (message->message_id) {
    case TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_MSG_ID:
        if (message->length != TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_SIZE) {
            return;
        }
        event.type = EVENT_TLM_TIME_SYNC;
        break;

    case TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_MSG_ID:
        if (message->length != TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_SIZE) {
            return;
        }
        event.type = EVENT_TLM_MAGFIELD;
        break;

    case TRANSPORT_KT_SP_MCLWAIN_MSG_ID:
        if (message->length != TRANSPORT_KT_SP_MCLWAIN_SIZE) {
            return;
        }
        event.type = EVENT_TLM_ORBIT;
        break;

    default:
        return;
    }

    (void)system_event_queue_push_back(&event);
}

static bool transport_message_is_for_this_node(const UnicanMessage* message) {
    return (message->address_to == transport_local_address) ||
        (message->address_to == UNICAN_NA_ADDRESS);
}

static void transport_handle_message(SystemContext* ctx,
                                     const UnicanMessage* message) {
    if ((ctx == NULL) || (message == NULL)) {
        return;
    }

    if (!transport_message_is_for_this_node(message)) {
        return;
    }

    if (transport_is_known_command_id(message->message_id)) {
        transport_handle_known_command(ctx, message);
        return;
    }

    if (transport_is_known_telemetry_command_id(message->message_id)) {
        transport_handle_known_telemetry(ctx, message);
        return;
    }

    (void)transport_send_ack(message->message_id, TRANSPORT_ACK_ERR_MSG_ID);
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

        transport_handle_message(ctx, &message);
    }
}
