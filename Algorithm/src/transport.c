#include "transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "event_queue.h"
#include "mram_store.h"
#include "tlm_staging.h"
#include "unican_version.h"

#ifdef NATALIA_ENABLE_DETECTOR_PROTO_LOG
#include "detector_log.h"
#endif

#define TRANSPORT_KU_TELEM_REQ_MSG_ID              (0x0F00U)
#define TRANSPORT_KU_STATUS_REQ_MSG_ID             (0x0F01U)
#define TRANSPORT_KU_SET_TIME_MSG_ID               (0x0F02U)
#define TRANSPORT_KU_OBSERVE_START_MSG_ID          (0x0F03U)
#define TRANSPORT_KU_OBSERVE_CTRL_MSG_ID           (0x0F04U)
#define TRANSPORT_KU_DUTY_MSG_ID                   (0x0F05U)
#define TRANSPORT_KU_DUMP_MSG_ID                   (0x0F06U)
#define TRANSPORT_KU_SET_CFG_MSG_ID                (0x0F07U)
#define TRANSPORT_KU_ERASE_MSG_ID                  (0x0F08U)
#define TRANSPORT_KU_TEST_MSG_ID                   (0x0F09U)
#define TRANSPORT_KU_TEST_RESULT_MSG_ID            (0x0F0AU)
#define TRANSPORT_KU_SHUTDOWN_MSG_ID               (0x0F0BU)
#define TRANSPORT_KU_RESET_ALARM_MSG_ID            (0x0F0CU)
#define TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID      (0x0401U)
#define TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID     (0x0A61U)
#define TRANSPORT_KU_SET_DEVICE_ID_MSG_ID          (0x0A62U)
#define TRANSPORT_KU_VERSION_REQ_MSG_ID            (0xFFE0U)

#define TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_MSG_ID (0xF210U)
#define TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_MSG_ID   (0xF221U)
#define TRANSPORT_KT_SP_MCLWAIN_MSG_ID             (0x0E00U)

#define TRANSPORT_KT_SP_TIME_ORBIT_ATTITUDE_SIZE   (125U)
#define TRANSPORT_KT_SP_MAGFIELD_ATTITUDE_SIZE     (76U)
#define TRANSPORT_KT_SP_MCLWAIN_SIZE               (24U)

#define TRANSPORT_TS_STATUS_MSG_ID                 (0x0D00U)
#define TRANSPORT_TS_ACK_MSG_ID                    (0x0D01U)
#define TRANSPORT_TS_TELEMETRY_MSG_ID              (0x0D02U)
#define TRANSPORT_TS_TEST_RESULT_MSG_ID            (0x0D03U)
#define TRANSPORT_TS_VERSION_MSG_ID                (0xFFE1U)

#define TRANSPORT_TELEMETRY_SIZE                   (109U)

#define TRANSPORT_SHORT_PAYLOAD_SIZE               (6U)
#define TRANSPORT_SET_CFG_PAYLOAD_SIZE             (68U)
#define TRANSPORT_FILL_BYTE                        (0xAAU)
#define TRANSPORT_TX_QUEUE_LENGTH                  (4U)
#define TRANSPORT_TX_MAX_RETRIES                   (3U)

#define TRANSPORT_CAN_CONTROL_DEST_FROM_SENDER     (0x0001U)
#define TRANSPORT_CAN_CONTROL_IGNORE_SPUTNIKS_TIME (0x0002U)

static uint8_t transport_rx_buffer[BOARD_COMM_MAX_MESSAGE_DATA];

typedef struct {
    uint16_t message_id;
    uint16_t address_to;
    uint16_t length;
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];
    const uint8_t* long_data;
    uint8_t retries_done;
} TransportTxItem;

static TransportTxItem transport_tx_queue[TRANSPORT_TX_QUEUE_LENGTH];

static uint8_t transport_long_tx_buffer[BOARD_MRAM_TEST_RESULT_IMAGE_SIZE];
static bool transport_long_tx_busy = false;
static uint16_t transport_tx_head = 0U;
static uint16_t transport_tx_tail = 0U;
static uint16_t transport_tx_count = 0U;

static bool transport_tx_active = false;
static uint32_t transport_tx_ok_snapshot = 0U;
static uint32_t transport_tx_failed_snapshot = 0U;

static uint16_t transport_remote_address = BOARD_COMM_ADDR_BVS;
static uint16_t transport_local_address = BOARD_COMM_ADDR_NA;
static uint16_t transport_can_control = 0U;
static uint16_t transport_last_sender = BOARD_COMM_ADDR_BVS;

void transport_reset(void) {
    memset(transport_tx_queue, 0, sizeof(transport_tx_queue));
    memset(transport_long_tx_buffer, 0, sizeof(transport_long_tx_buffer));
    transport_long_tx_busy = false;
    transport_tx_head = 0U;
    transport_tx_tail = 0U;
    transport_tx_count = 0U;
    transport_tx_active = false;
    transport_tx_ok_snapshot = 0U;
    transport_tx_failed_snapshot = 0U;
    transport_remote_address = BOARD_COMM_ADDR_BVS;
    transport_local_address = BOARD_COMM_ADDR_NA;
    transport_can_control = 0U;
    transport_last_sender = BOARD_COMM_ADDR_BVS;
}

void transport_apply_stored_addresses(uint16_t device_id,
                                      uint16_t destination_id) {
    if (device_id != 0U) {
        transport_local_address = device_id;
    }

    if (destination_id != 0U) {
        transport_remote_address = destination_id;
    }
}

static uint16_t transport_next_tx_index(uint16_t index) {
    ++index;

    if (index >= TRANSPORT_TX_QUEUE_LENGTH) {
        index = 0U;
    }

    return index;
}

static uint16_t transport_effective_destination(void) {
    if ((transport_can_control & TRANSPORT_CAN_CONTROL_DEST_FROM_SENDER) != 0U) {
        return transport_last_sender;
    }

    return transport_remote_address;
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
    item->address_to = transport_effective_destination();
    item->length = TRANSPORT_SHORT_PAYLOAD_SIZE;
    item->long_data = NULL;
    item->retries_done = 0U;

    memcpy(item->payload, payload, TRANSPORT_SHORT_PAYLOAD_SIZE);

    transport_tx_head = transport_next_tx_index(transport_tx_head);
    ++transport_tx_count;

    return BOARD_OK;
}

static BoardStatus transport_enqueue_long_message(uint16_t message_id,
                                                  const uint8_t* data,
                                                  uint16_t length) {
    TransportTxItem* item;

    if ((data == NULL) || (length == 0U) ||
        (length > sizeof(transport_long_tx_buffer))) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (transport_long_tx_busy) {
        return BOARD_ERR_BUSY;
    }

    if (transport_tx_count >= TRANSPORT_TX_QUEUE_LENGTH) {
        return BOARD_ERR_BUSY;
    }

    memcpy(transport_long_tx_buffer, data, length);
    transport_long_tx_busy = true;

    item = &transport_tx_queue[transport_tx_head];

    item->message_id = message_id;
    item->address_to = transport_effective_destination();
    item->length = length;
    item->long_data = transport_long_tx_buffer;
    item->retries_done = 0U;

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

    if (item->long_data != NULL) {
        transport_long_tx_busy = false;
    }

    item->message_id = 0U;
    item->address_to = 0U;
    item->length = 0U;
    item->long_data = NULL;
    item->retries_done = 0U;

    memset(item->payload, 0, sizeof(item->payload));

    transport_tx_tail = transport_next_tx_index(transport_tx_tail);
    --transport_tx_count;
}

static BoardStatus transport_service_tx(void) {
    BoardCommStatus protocol_status;
    BoardCommMessage message;
    TransportTxItem* item;
    BoardStatus status;

    board_comm_get_status(&protocol_status);

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

    board_comm_get_status(&protocol_status);

    if (protocol_status.tx_busy) {
        return BOARD_OK;
    }

    item = &transport_tx_queue[transport_tx_tail];

    message.message_id = item->message_id;
    message.address_from = transport_local_address;
    message.address_to = item->address_to;
    message.length = item->length;
    message.data = (item->long_data != NULL) ? item->long_data : item->payload;

    transport_tx_ok_snapshot = protocol_status.tx_messages_ok;
    transport_tx_failed_snapshot = protocol_status.tx_messages_failed;

    status = board_comm_send(&message);

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

static void transport_write_le_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void transport_write_le_u24(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
    data[2] = (uint8_t)((value >> 16U) & 0xFFU);
}

static void transport_write_le_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
    data[2] = (uint8_t)((value >> 16U) & 0xFFU);
    data[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint16_t transport_temp_milli_to_deci(int32_t milli_c) {
    int32_t deci = milli_c / 100;
    if (deci > 32767) {
        deci = 32767;
    } else if (deci < -32768) {
        deci = -32768;
    }
    return (uint16_t)(int16_t)deci;
}

static uint16_t transport_current_ua_to_ma(int32_t current_ua) {
    int32_t milli_amp = current_ua / 1000;
    if (milli_amp < 0) {
        milli_amp = 0;
    } else if (milli_amp > 65535) {
        milli_amp = 65535;
    }
    return (uint16_t)milli_amp;
}

static uint16_t transport_clamp_u16(uint32_t value) {
    return (value > 0xFFFFU) ? 0xFFFFU : (uint16_t)value;
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
    case TRANSPORT_KU_VERSION_REQ_MSG_ID:
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

BoardStatus transport_send_dump_ack(uint16_t command_id,
                                    TransportAckStatus status,
                                    uint32_t packet_count) {
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];

    payload[0] = (uint8_t)(command_id & 0xFFU);
    payload[1] = (uint8_t)(command_id >> 8U);
    payload[2] = (uint8_t)status;
    payload[3] = (uint8_t)(packet_count & 0xFFU);
    payload[4] = (uint8_t)((packet_count >> 8U) & 0xFFU);
    payload[5] = (uint8_t)((packet_count >> 16U) & 0xFFU);

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

BoardStatus transport_send_telemetry(const SystemContext* ctx) {
    uint8_t buffer[TRANSPORT_TELEMETRY_SIZE];
    InstrumentTime rtc = {0};
    BoardDigitalTempSample pu_temp = {0};
    BoardDigitalTempSample ped_temp = {0};
    BoardTempSample bd_temp = {0};
    BoardPowerSample pu_power = {0};
    BoardPowerSample ped_power = {0};
    uint32_t board_status_word = 0U;
    MramStoreConfig cfg = {0};
    MramStoreServiceData svc = {0};

    if (ctx == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(buffer, 0, sizeof(buffer));

    (void)board_rtc_get_time(&rtc);
    transport_write_le_u16(&buffer[0], rtc.milliseconds);
    transport_write_le_u32(&buffer[2], rtc.seconds);

    /* Bytes 6-7 (MC temp) stay 0: the STM32 internal temperature is not exposed
     * by Board_API. */
    if (board_read_digital_temp(BOARD_TEMP_SENSOR_PU, &pu_temp) == BOARD_OK) {
        transport_write_le_u16(&buffer[8],
            transport_temp_milli_to_deci(pu_temp.temperature_milli_c));
    }
    if (board_read_digital_temp(BOARD_TEMP_SENSOR_PED, &ped_temp) == BOARD_OK) {
        transport_write_le_u16(&buffer[10],
            transport_temp_milli_to_deci(ped_temp.temperature_milli_c));
    }
    if (board_read_temp(&bd_temp) == BOARD_OK) {
        transport_write_le_u16(&buffer[12],
            transport_temp_milli_to_deci(bd_temp.temperature_milli_c));
    }

    if (board_read_power_monitor(BOARD_POWER_MONITOR_PU, &pu_power) == BOARD_OK) {
        transport_write_le_u16(&buffer[14], transport_clamp_u16(pu_power.bus_voltage_mv));
        transport_write_le_u16(&buffer[16], transport_current_ua_to_ma(pu_power.current_ua));
    }
    if (board_read_power_monitor(BOARD_POWER_MONITOR_PED, &ped_power) == BOARD_OK) {
        transport_write_le_u16(&buffer[18], transport_clamp_u16(ped_power.bus_voltage_mv));
        transport_write_le_u16(&buffer[20], transport_current_ua_to_ma(ped_power.current_ua));
    }

    transport_write_le_u16(&buffer[22], (uint16_t)(ctx->alarm_status & 0xFFFFU));
    transport_write_le_u16(&buffer[24], (uint16_t)(ctx->masked_alarm & 0xFFFFU));
    buffer[26] = transport_build_mode_byte(ctx);
    buffer[27] = transport_build_nand_full_byte(ctx);

    (void)board_read_power_status(&board_status_word);
    transport_write_le_u16(&buffer[28], (uint16_t)(board_status_word & 0xFFFFU));

    /* Bytes 30-35 (PED status, trigger config, observe settings) are 0 in modes
     * other than OBSERVE; OBSERVE is not implemented. */

    (void)mram_store_load_config(&cfg);
    (void)mram_store_load_service_data(&svc);

    transport_write_le_u16(&buffer[36], (uint16_t)cfg.mcu_pu_temp_min);
    transport_write_le_u16(&buffer[38], (uint16_t)cfg.mcu_pu_temp_max);
    transport_write_le_u16(&buffer[40], (uint16_t)cfg.pu_temp_min);
    transport_write_le_u16(&buffer[42], (uint16_t)cfg.pu_temp_max);
    transport_write_le_u16(&buffer[44], (uint16_t)cfg.ped_temp_min);
    transport_write_le_u16(&buffer[46], (uint16_t)cfg.ped_temp_max);
    transport_write_le_u16(&buffer[48], (uint16_t)cfg.det_temp_min);
    transport_write_le_u16(&buffer[50], (uint16_t)cfg.det_temp_max);
    transport_write_le_u16(&buffer[52], cfg.pu_voltage_min);
    transport_write_le_u16(&buffer[54], cfg.pu_voltage_max);
    transport_write_le_u16(&buffer[56], cfg.pu_current_min);
    transport_write_le_u16(&buffer[58], cfg.pu_current_max);
    transport_write_le_u16(&buffer[60], cfg.ped_voltage_min);
    transport_write_le_u16(&buffer[62], cfg.ped_voltage_max);
    transport_write_le_u16(&buffer[64], cfg.ped_current_min);
    transport_write_le_u16(&buffer[66], cfg.ped_current_max);
    transport_write_le_u16(&buffer[68], (uint16_t)cfg.belt_lmin);
    transport_write_le_u16(&buffer[70], (uint16_t)cfg.belt_lmax);
    transport_write_le_u16(&buffer[72], (uint16_t)cfg.belt_bmin);
    transport_write_le_u16(&buffer[74], cfg.ac1_rate_max);
    transport_write_le_u16(&buffer[76], cfg.init_rtc_time_ms);
    transport_write_le_u32(&buffer[78], cfg.init_rtc_time);
    transport_write_le_u16(&buffer[82], svc.observe_session_id);
    transport_write_le_u24(&buffer[84], svc.nand1_packet_count);
    transport_write_le_u24(&buffer[87], svc.nand2_packet_count);
    transport_write_le_u16(&buffer[90], svc.nand1_erase_count);
    transport_write_le_u16(&buffer[92], svc.nand2_erase_count);
    transport_write_le_u16(&buffer[94], svc.nand1_test_count);
    transport_write_le_u16(&buffer[96], svc.nand2_test_count);
    transport_write_le_u16(&buffer[98], cfg.alarm_mask);
    transport_write_le_u16(&buffer[100], cfg.can_control);
    transport_write_le_u16(&buffer[102], cfg.destination_id);
    transport_write_le_u16(&buffer[104], cfg.device_id);

    buffer[106] = NATALIA_SW_VERSION_MAJOR;
    buffer[107] = NATALIA_SW_VERSION_MINOR;
    buffer[108] = NATALIA_SW_VERSION_EXTRA;

    return transport_enqueue_long_message(TRANSPORT_TS_TELEMETRY_MSG_ID,
                                          buffer, TRANSPORT_TELEMETRY_SIZE);
}

BoardStatus transport_send_version(void) {
    uint8_t payload[TRANSPORT_SHORT_PAYLOAD_SIZE];

    payload[0] = NATALIA_SW_VERSION_MAJOR;
    payload[1] = NATALIA_SW_VERSION_MINOR;
    payload[2] = NATALIA_SW_VERSION_EXTRA;
    payload[3] = TRANSPORT_FILL_BYTE;
    payload[4] = TRANSPORT_FILL_BYTE;
    payload[5] = TRANSPORT_FILL_BYTE;

    return transport_enqueue_short_message(TRANSPORT_TS_VERSION_MSG_ID,
                                           payload);
}

BoardStatus transport_send_test_result(const uint8_t* data, uint16_t length) {
    return transport_enqueue_long_message(TRANSPORT_TS_TEST_RESULT_MSG_ID,
                                          data, length);
}

static bool transport_payload_is_fill_range(const BoardCommMessage* message,
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

static bool transport_is_short_message(const BoardCommMessage* message) {
    return (message != NULL) &&
        (message->data != NULL) &&
        (message->length == TRANSPORT_SHORT_PAYLOAD_SIZE);
}

static BoardStatus transport_build_fill_command_event(const BoardCommMessage* message,
                                                      SystemEvent* event,
                                                      EventType type) {
    if ((message == NULL) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = type;
    event->msg_id = message->message_id;

    return BOARD_OK;
}

static BoardStatus transport_build_set_time_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_sputniks_set_time_event(const BoardCommMessage* message,
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

    if ((event_mode == 0U) != (event_count == 0U)) {
        return false;
    }

    if ((spectrum_mode == 0U) && (hist_mode != 0U)) {
        return false;
    }

    if ((spectrum_mode == 1U) && (hist_mode == 0U)) {
        return false;
    }

    if ((spectrum_mode == 2U) && (hist_mode != 0U)) {
        return false;
    }

    return true;
}

static BoardStatus transport_build_observe_start_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_observe_ctrl_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_duty_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_dump_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_set_cfg_event(const BoardCommMessage* message,
                                                 SystemEvent* event) {
    const uint8_t* data;
    CmdSetConfig* cfg;
    uint16_t write_control;
    uint16_t can_control;

    if ((message == NULL) || (event == NULL) || (message->data == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->length != TRANSPORT_SET_CFG_PAYLOAD_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    data = message->data;

    write_control = transport_read_le_u16(&data[0]);
    can_control = transport_read_le_u16(&data[66]);

    if ((write_control & 0xFF80U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((can_control & 0xFFFCU) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_SET_CFG;
    event->msg_id = message->message_id;

    cfg = &event->command.set_config;
    cfg->write_control = write_control;
    cfg->mcu_pu_temp_min = (int16_t)transport_read_le_u16(&data[2]);
    cfg->mcu_pu_temp_max = (int16_t)transport_read_le_u16(&data[4]);
    cfg->pu_temp_min = (int16_t)transport_read_le_u16(&data[6]);
    cfg->pu_temp_max = (int16_t)transport_read_le_u16(&data[8]);
    cfg->ped_temp_min = (int16_t)transport_read_le_u16(&data[10]);
    cfg->ped_temp_max = (int16_t)transport_read_le_u16(&data[12]);
    cfg->det_temp_min = (int16_t)transport_read_le_u16(&data[14]);
    cfg->det_temp_max = (int16_t)transport_read_le_u16(&data[16]);
    cfg->pu_voltage_min = transport_read_le_u16(&data[18]);
    cfg->pu_voltage_max = transport_read_le_u16(&data[20]);
    cfg->pu_current_min = transport_read_le_u16(&data[22]);
    cfg->pu_current_max = transport_read_le_u16(&data[24]);
    cfg->ped_voltage_min = transport_read_le_u16(&data[26]);
    cfg->ped_voltage_max = transport_read_le_u16(&data[28]);
    cfg->ped_current_min = transport_read_le_u16(&data[30]);
    cfg->ped_current_max = transport_read_le_u16(&data[32]);
    cfg->belt_lmin = (int16_t)transport_read_le_u16(&data[34]);
    cfg->belt_lmax = (int16_t)transport_read_le_u16(&data[36]);
    cfg->belt_bmin = (int16_t)transport_read_le_u16(&data[38]);
    cfg->ac1_rate_max = transport_read_le_u16(&data[40]);
    cfg->init_rtc_time_ms = transport_read_le_u16(&data[42]);
    cfg->init_rtc_time = transport_read_le_u32(&data[44]);
    cfg->observe_session_id = transport_read_le_u16(&data[48]);
    cfg->nand1_packet_count = transport_read_le_u24(&data[50]);
    cfg->nand2_packet_count = transport_read_le_u24(&data[53]);
    cfg->nand1_erase_count = transport_read_le_u16(&data[56]);
    cfg->nand2_erase_count = transport_read_le_u16(&data[58]);
    cfg->nand1_test_count = transport_read_le_u16(&data[60]);
    cfg->nand2_test_count = transport_read_le_u16(&data[62]);
    cfg->alarm_mask = transport_read_le_u16(&data[64]);
    cfg->can_control = can_control;

    return BOARD_OK;
}

static BoardStatus transport_build_erase_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_test_event(const BoardCommMessage* message,
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

static BoardStatus transport_build_test_result_event(const BoardCommMessage* message,
                                                     SystemEvent* event) {
    uint8_t config;
    uint8_t bank_id;
    uint8_t mram_copy;

    if (!transport_is_short_message(message) || (event == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!transport_payload_is_fill_range(message, 1U, TRANSPORT_SHORT_PAYLOAD_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    config = message->data[0];
    bank_id = config & 0x03U;
    mram_copy = (config >> 2U) & 0x03U;

    if (!transport_is_valid_bank(bank_id)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((mram_copy != 1U) && (mram_copy != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((config & 0xF0U) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(event, 0, sizeof(*event));

    event->type = EVENT_CMD_TEST_RESULT;
    event->msg_id = message->message_id;
    event->command.test_result.bank = transport_decode_bank(bank_id);
    event->command.test_result.mram_copy = mram_copy;

    return BOARD_OK;
}

static BoardStatus transport_build_command_event(const BoardCommMessage* message,
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

    case TRANSPORT_KU_VERSION_REQ_MSG_ID:
        return transport_build_fill_command_event(message,
                                                  event,
                                                  EVENT_CMD_VERSION_REQ);

    case TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID:
        return transport_build_sputniks_set_time_event(message, event);

    default:
        return BOARD_ERR_INVALID_ARG;
    }
}

static BoardStatus transport_handle_address_command(const BoardCommMessage* message) {
    if (!transport_is_short_message(message)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (message->message_id == TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID) {
        uint16_t destination_id = transport_read_le_u16(&message->data[0]);

        if (destination_id == 0U) {
            return BOARD_ERR_INVALID_ARG;
        }

        transport_remote_address = destination_id;
        return BOARD_OK;
    }

    if (message->message_id == TRANSPORT_KU_SET_DEVICE_ID_MSG_ID) {
        uint16_t device_id = transport_read_le_u16(&message->data[0]);

        if (device_id == 0U) {
            return BOARD_ERR_INVALID_ARG;
        }

        transport_local_address = device_id;
        return BOARD_OK;
    }

    return BOARD_ERR_INVALID_ARG;
}

static void transport_handle_known_command(SystemContext* ctx,
                                           const BoardCommMessage* message) {
    BoardStatus status;
    SystemEvent event;

    if ((message->message_id == TRANSPORT_KU_SPUTNIKS_SET_TIME_MSG_ID) &&
        ((ctx->can_control & TRANSPORT_CAN_CONTROL_IGNORE_SPUTNIKS_TIME) != 0U)) {
        return;
    }

    if ((message->message_id == TRANSPORT_KU_SET_DESTINATION_ID_MSG_ID) ||
        (message->message_id == TRANSPORT_KU_SET_DEVICE_ID_MSG_ID)) {
        status = transport_handle_address_command(message);

        if (status != BOARD_OK) {
            (void)transport_send_ack(message->message_id, TRANSPORT_ACK_ERR_CONTENT);
            return;
        }

        if (ctx->state != STATE_SHUTDOWN) {
            if (mram_store_save_addresses(transport_local_address,
                                          transport_remote_address) != BOARD_OK) {
                (void)transport_send_ack(message->message_id, TRANSPORT_ACK_ERR_OTHER);
                return;
            }
        }

        (void)transport_send_ack(message->message_id, TRANSPORT_ACK_OK);
        return;
    }

    status = transport_build_command_event(message, &event);

    if (status != BOARD_OK) {
        (void)transport_send_ack(message->message_id,
                                 TRANSPORT_ACK_ERR_CONTENT);
        return;
    }

    (void)system_event_queue_push_back(&event);
}

static void transport_handle_known_telemetry(SystemContext* ctx,
                                             const BoardCommMessage* message) {
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

    event.tlm_slot = tlm_staging_put(message->data, message->length);

    (void)system_event_queue_push_back(&event);
}

static bool transport_message_is_for_this_node(const BoardCommMessage* message) {
    return (message->address_to == transport_local_address) ||
        (message->address_to == BOARD_COMM_ADDR_NA);
}

static void transport_handle_message(SystemContext* ctx,
                                     const BoardCommMessage* message) {
    if ((ctx == NULL) || (message == NULL)) {
        return;
    }

    if (!transport_message_is_for_this_node(message)) {
        return;
    }

    transport_last_sender = message->address_from;

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
    BoardCommMessage message;

    if (ctx == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    transport_can_control = ctx->can_control;

    board_comm_poll(now_ms);

    tx_status = transport_service_tx();

    while (true) {
        status = board_comm_receive(&message,
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
