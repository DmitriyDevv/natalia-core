#include "unican.h"

#include <stddef.h>
#include <string.h>

#include "unican_crc.h"
#include "unican_port.h"

/* ------------------------------------------------------------------------- */
/* Internal types                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    bool used;

    uint16_t message_id;
    uint16_t address_from;
    uint16_t address_to;
    uint16_t length;

    uint8_t data[UNICAN_MAX_MESSAGE_DATA];
} UnicanSlot;

typedef struct {
    bool active;

    uint16_t address_from;
    uint16_t address_to;

    uint16_t expected_total_length; /* payload + CRC */
    uint16_t position;
    uint16_t payload_length;

    uint8_t crc_bytes[UNICAN_CRC_LENGTH];

    uint32_t last_update_ms;
    uint8_t slot_index;
} UnicanRxBuffer;

typedef enum {
    UNICAN_TX_PHASE_IDLE = 0,

    UNICAN_TX_PHASE_SHORT_SEND,
    UNICAN_TX_PHASE_SHORT_WAIT,

    UNICAN_TX_PHASE_LONG_START_SEND,
    UNICAN_TX_PHASE_LONG_START_WAIT,

    UNICAN_TX_PHASE_LONG_DATA_SEND,
    UNICAN_TX_PHASE_LONG_DATA_WAIT
} UnicanTxPhase;

typedef struct {
    bool active;
    bool abort_in_progress;

    UnicanTxPhase phase;

    uint16_t message_id;
    uint16_t address_from;
    uint16_t address_to;
    uint16_t length;

    uint8_t data[UNICAN_MAX_MESSAGE_DATA];

    uint16_t crc;
    uint16_t stream_position;

    uint32_t pending_sequence;
    uint32_t pending_since_ms;
    uint8_t pending_data_length;
} UnicanTxContext;

/* ------------------------------------------------------------------------- */
/* Internal state                                                             */
/* ------------------------------------------------------------------------- */

static UnicanStatus unican_status;

static UnicanSlot unican_slots[UNICAN_MESSAGE_SLOTS_COUNT];
static UnicanRxBuffer unican_rx_buffers[UNICAN_RX_BUFFERS_COUNT];

static uint8_t unican_ready_queue[UNICAN_MESSAGE_SLOTS_COUNT];
static uint16_t unican_ready_head = 0U;
static uint16_t unican_ready_tail = 0U;
static uint16_t unican_ready_count = 0U;

static UnicanTxContext unican_tx;


/* Generic helpers                                                            */
/* ------------------------------------------------------------------------- */

static uint16_t unican_next_queue_index(uint16_t index, uint16_t length) {
    ++index;

    if (index >= length) {
        index = 0U;
    }

    return index;
}

static void unican_record_error(uint16_t error) {
    unican_status.last_error = error;
}

static void unican_status_recount(void) {
    uint16_t index;
    uint16_t free_slots = 0U;
    uint16_t active_rx_buffers = 0U;

    for (index = 0U; index < UNICAN_MESSAGE_SLOTS_COUNT; ++index) {
        if (!unican_slots[index].used) {
            ++free_slots;
        }
    }

    for (index = 0U; index < UNICAN_RX_BUFFERS_COUNT; ++index) {
        if (unican_rx_buffers[index].active) {
            ++active_rx_buffers;
        }
    }

    unican_status.free_slots_count = free_slots;
    unican_status.ready_messages_count = unican_ready_count;
    unican_status.active_rx_buffers_count = active_rx_buffers;
    unican_status.tx_busy = unican_tx.active;
}

static int32_t unican_slot_allocate(void) {
    uint16_t index;

    for (index = 0U; index < UNICAN_MESSAGE_SLOTS_COUNT; ++index) {
        if (!unican_slots[index].used) {
            unican_slots[index].used = true;
            unican_slots[index].message_id = 0U;
            unican_slots[index].address_from = 0U;
            unican_slots[index].address_to = 0U;
            unican_slots[index].length = 0U;

            unican_status_recount();

            return (int32_t)index;
        }
    }

    return -1;
}

static void unican_slot_free(uint8_t slot_index) {
    if (slot_index >= UNICAN_MESSAGE_SLOTS_COUNT) {
        return;
    }

    unican_slots[slot_index].used = false;
    unican_slots[slot_index].message_id = 0U;
    unican_slots[slot_index].address_from = 0U;
    unican_slots[slot_index].address_to = 0U;
    unican_slots[slot_index].length = 0U;

    unican_status_recount();
}

static bool unican_ready_push(uint8_t slot_index) {
    if (unican_ready_count >= UNICAN_MESSAGE_SLOTS_COUNT) {
        ++unican_status.dropped_messages;
        unican_record_error(UNICAN_READY_QUEUE_FULL);
        return false;
    }

    unican_ready_queue[unican_ready_head] = slot_index;
    unican_ready_head =
        unican_next_queue_index(unican_ready_head,
                                UNICAN_MESSAGE_SLOTS_COUNT);
    ++unican_ready_count;

    unican_status_recount();

    return true;
}

static bool unican_ready_peek(uint8_t* slot_index) {
    if ((slot_index == NULL) || (unican_ready_count == 0U)) {
        return false;
    }

    *slot_index = unican_ready_queue[unican_ready_tail];

    return true;
}

static void unican_ready_pop(void) {
    if (unican_ready_count == 0U) {
        return;
    }

    unican_ready_tail =
        unican_next_queue_index(unican_ready_tail,
                                UNICAN_MESSAGE_SLOTS_COUNT);
    --unican_ready_count;

    unican_status_recount();
}

/* ------------------------------------------------------------------------- */
/* CAN identifier conversion                                                  */
/* ------------------------------------------------------------------------- */

static void unican_extract_identifier(const CanFrame* frame,
                                      uint16_t* address_to,
                                      uint16_t* address_from,
                                      bool* data_frame) {
    if (frame->id_type == CAN_FRAME_STANDARD_ID) {
        *address_to = (uint16_t)(frame->identifier & 0x1FUL);
        *address_from =
            (uint16_t)((frame->identifier >> 5U) & 0x1FUL);
        *data_frame =
            (((frame->identifier >> 10U) & 0x01UL) != 0U);
    } else {
        *address_to = (uint16_t)(frame->identifier & 0x3FFFUL);
        *address_from =
            (uint16_t)((frame->identifier >> 14U) & 0x3FFFUL);
        *data_frame =
            (((frame->identifier >> 28U) & 0x01UL) != 0U);
    }
}

static void unican_set_identifier(CanFrame* frame,
                                  uint16_t address_from,
                                  uint16_t address_to,
                                  bool data_frame) {
    if ((address_from > 31U) || (address_to > 31U)) {
        frame->id_type = CAN_FRAME_EXTENDED_ID;
        frame->identifier =
            (uint32_t)address_to |
            ((uint32_t)address_from << 14U);

        if (data_frame) {
            frame->identifier |= (1UL << 28U);
        }
    } else {
        frame->id_type = CAN_FRAME_STANDARD_ID;
        frame->identifier =
            (uint32_t)address_to |
            ((uint32_t)address_from << 5U);

        if (data_frame) {
            frame->identifier |= (1UL << 10U);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* RX logic                                                                   */
/* ------------------------------------------------------------------------- */

static UnicanRxBuffer* unican_rx_buffer_find_by_sender(uint16_t address_from) {
    uint16_t index;

    for (index = 0U; index < UNICAN_RX_BUFFERS_COUNT; ++index) {
        if (unican_rx_buffers[index].active &&
            (unican_rx_buffers[index].address_from == address_from)) {
            return &unican_rx_buffers[index];
        }
    }

    return NULL;
}

static UnicanRxBuffer* unican_rx_buffer_find_free(void) {
    uint16_t index;

    for (index = 0U; index < UNICAN_RX_BUFFERS_COUNT; ++index) {
        if (!unican_rx_buffers[index].active) {
            return &unican_rx_buffers[index];
        }
    }

    return NULL;
}

static void unican_rx_buffer_release_without_slot_free(UnicanRxBuffer* buffer) {
    if (buffer == NULL) {
        return;
    }

    buffer->active = false;
    buffer->address_from = 0U;
    buffer->address_to = 0U;
    buffer->expected_total_length = 0U;
    buffer->position = 0U;
    buffer->payload_length = 0U;
    buffer->crc_bytes[0] = 0U;
    buffer->crc_bytes[1] = 0U;
    buffer->last_update_ms = 0U;
    buffer->slot_index = 0U;

    unican_status_recount();
}

static void unican_rx_buffer_reset(UnicanRxBuffer* buffer) {
    if (buffer == NULL) {
        return;
    }

    if (buffer->active) {
        unican_slot_free(buffer->slot_index);
    }

    unican_rx_buffer_release_without_slot_free(buffer);
}

static bool unican_can_frame_is_valid(const CanFrame* frame) {
    if (frame == NULL) {
        return false;
    }

    if (frame->dlc > CAN_FRAME_MAX_DATA_SIZE) {
        return false;
    }

    if (frame->frame_type != CAN_FRAME_DATA) {
        return false;
    }

    return true;
}

static void unican_complete_long_message(UnicanRxBuffer* buffer) {
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint8_t slot_index;

    if ((buffer == NULL) || (!buffer->active)) {
        return;
    }

    slot_index = buffer->slot_index;

    received_crc =
        (uint16_t)buffer->crc_bytes[0] |
        ((uint16_t)buffer->crc_bytes[1] << 8U);

    calculated_crc =
        unican_crc16_xmodem(unican_slots[slot_index].data,
                            unican_slots[slot_index].length);

    if (received_crc != calculated_crc) {
        unican_slot_free(slot_index);
        unican_rx_buffer_release_without_slot_free(buffer);

        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_WRONG_CRC);

        return;
    }

    if (!unican_ready_push(slot_index)) {
        unican_slot_free(slot_index);
        unican_rx_buffer_release_without_slot_free(buffer);
        return;
    }

    ++unican_status.rx_messages_ok;

    unican_rx_buffer_release_without_slot_free(buffer);
}

static void unican_process_long_data_frame(const CanFrame* frame,
                                           uint16_t address_from,
                                           uint32_t now_ms) {
    UnicanRxBuffer* buffer;
    UnicanSlot* slot;
    uint16_t index;

    buffer = unican_rx_buffer_find_by_sender(address_from);

    if ((buffer == NULL) || (!buffer->active)) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_DATA_WITHOUT_START);
        return;
    }

    slot = &unican_slots[buffer->slot_index];
    buffer->last_update_ms = now_ms;

    for (index = 0U; index < frame->dlc; ++index) {
        if (buffer->position >= buffer->expected_total_length) {
            ++unican_status.rx_transport_errors;
            unican_record_error(UNICAN_WARNING_UNEXPECTED_DATA);
            return;
        }

        if (buffer->position < buffer->payload_length) {
            slot->data[buffer->position] = frame->data[index];
        } else {
            buffer->crc_bytes[buffer->position - buffer->payload_length] =
                frame->data[index];
        }

        ++buffer->position;
    }

    if (buffer->position == buffer->expected_total_length) {
        unican_complete_long_message(buffer);
    }
}

static void unican_process_long_start_frame(const CanFrame* frame,
                                            uint16_t address_from,
                                            uint16_t address_to,
                                            uint32_t now_ms) {
    UnicanRxBuffer* buffer;
    int32_t slot_index;
    uint16_t message_id;
    uint16_t total_length;
    uint16_t payload_length;

    if (frame->dlc < 6U) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_HEADER_TOO_SHORT);
        return;
    }

    message_id =
        (uint16_t)frame->data[2] |
        ((uint16_t)frame->data[3] << 8U);

    total_length =
        (uint16_t)frame->data[4] |
        ((uint16_t)frame->data[5] << 8U);

    if (total_length < UNICAN_CRC_LENGTH) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_WRONG_CRC);
        return;
    }

    payload_length = (uint16_t)(total_length - UNICAN_CRC_LENGTH);

    if (payload_length > UNICAN_MAX_MESSAGE_DATA) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_MESSAGE_TOO_LONG);
        return;
    }

    buffer = unican_rx_buffer_find_by_sender(address_from);

    if (buffer == NULL) {
        buffer = unican_rx_buffer_find_free();

        if (buffer == NULL) {
            ++unican_status.dropped_messages;
            unican_record_error(UNICAN_NO_FREE_BUFFER);
            return;
        }
    } else {
        unican_rx_buffer_reset(buffer);
        unican_record_error(UNICAN_WARNING_BUFFER_OVERWRITE);
    }

    slot_index = unican_slot_allocate();

    if (slot_index < 0) {
        ++unican_status.dropped_messages;
        unican_record_error(UNICAN_CANT_ALLOCATE_NODE);
        return;
    }

    unican_slots[slot_index].message_id = message_id;
    unican_slots[slot_index].address_from = address_from;
    unican_slots[slot_index].address_to = address_to;
    unican_slots[slot_index].length = payload_length;

    buffer->active = true;
    buffer->address_from = address_from;
    buffer->address_to = address_to;
    buffer->expected_total_length = total_length;
    buffer->position = 0U;
    buffer->payload_length = payload_length;
    buffer->crc_bytes[0] = 0U;
    buffer->crc_bytes[1] = 0U;
    buffer->last_update_ms = now_ms;
    buffer->slot_index = (uint8_t)slot_index;

    unican_status_recount();
}

static void unican_process_short_frame(const CanFrame* frame,
                                       uint16_t message_id,
                                       uint16_t address_from,
                                       uint16_t address_to) {
    int32_t slot_index;
    uint16_t payload_length;
    uint16_t index;

    payload_length = (uint16_t)(frame->dlc - 2U);

    slot_index = unican_slot_allocate();

    if (slot_index < 0) {
        ++unican_status.dropped_messages;
        unican_record_error(UNICAN_CANT_ALLOCATE_NODE);
        return;
    }

    unican_slots[slot_index].message_id = message_id;
    unican_slots[slot_index].address_from = address_from;
    unican_slots[slot_index].address_to = address_to;
    unican_slots[slot_index].length = payload_length;

    for (index = 0U; index < payload_length; ++index) {
        unican_slots[slot_index].data[index] = frame->data[index + 2U];
    }

    if (!unican_ready_push((uint8_t)slot_index)) {
        unican_slot_free((uint8_t)slot_index);
        return;
    }

    ++unican_status.rx_messages_ok;
}

static void unican_process_received_frame(const CanFrame* frame,
                                          uint32_t now_ms) {
    uint16_t address_to;
    uint16_t address_from;
    uint16_t message_id;
    bool data_frame;

    if (!unican_can_frame_is_valid(frame)) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_CAN_MESSAGE_TOO_LONG);
        return;
    }

    unican_extract_identifier(frame,
                              &address_to,
                              &address_from,
                              &data_frame);

    if (data_frame) {
        unican_process_long_data_frame(frame, address_from, now_ms);
        return;
    }

    if (frame->dlc < 2U) {
        ++unican_status.rx_transport_errors;
        unican_record_error(UNICAN_CAN_MESSAGE_TOO_SHORT);
        return;
    }

    message_id =
        (uint16_t)frame->data[0] |
        ((uint16_t)frame->data[1] << 8U);

    if (message_id == UNICAN_START_LONG_MESSAGE) {
        unican_process_long_start_frame(frame,
                                        address_from,
                                        address_to,
                                        now_ms);
    } else {
        unican_process_short_frame(frame,
                                   message_id,
                                   address_from,
                                   address_to);
    }
}

static void unican_check_rx_timeouts(uint32_t now_ms) {
    uint16_t index;

    for (index = 0U; index < UNICAN_RX_BUFFERS_COUNT; ++index) {
        if (unican_rx_buffers[index].active) {
            const uint32_t elapsed_ms =
                now_ms - unican_rx_buffers[index].last_update_ms;

            if (elapsed_ms >= UNICAN_LONG_MESSAGE_TIMEOUT_MS) {
                ++unican_status.timeout_messages;
                ++unican_status.rx_transport_errors;

                unican_rx_buffer_reset(&unican_rx_buffers[index]);
                unican_record_error(UNICAN_MESSAGE_TIMEOUT);
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* TX logic                                                                   */
/* ------------------------------------------------------------------------- */

static void unican_tx_clear(void) {
    unican_tx.active = false;
    unican_tx.abort_in_progress = false;
    unican_tx.phase = UNICAN_TX_PHASE_IDLE;

    unican_tx.message_id = 0U;
    unican_tx.address_from = 0U;
    unican_tx.address_to = 0U;
    unican_tx.length = 0U;

    unican_tx.crc = 0U;
    unican_tx.stream_position = 0U;

    unican_tx.pending_sequence = 0U;
    unican_tx.pending_since_ms = 0U;
    unican_tx.pending_data_length = 0U;

    unican_status_recount();
}

static void unican_prepare_common_tx_frame(CanFrame* frame, bool data_frame) {
    memset(frame, 0, sizeof(*frame));

    frame->frame_type = CAN_FRAME_DATA;

    unican_set_identifier(frame,
                          unican_tx.address_from,
                          unican_tx.address_to,
                          data_frame);
}

static void unican_prepare_short_tx_frame(CanFrame* frame) {
    uint16_t index;

    unican_prepare_common_tx_frame(frame, false);

    frame->dlc = (uint8_t)(unican_tx.length + 2U);
    frame->data[0] = (uint8_t)(unican_tx.message_id & 0x00FFU);
    frame->data[1] = (uint8_t)((unican_tx.message_id >> 8U) & 0x00FFU);

    for (index = 0U; index < unican_tx.length; ++index) {
        frame->data[index + 2U] = unican_tx.data[index];
    }
}

static void unican_prepare_long_start_tx_frame(CanFrame* frame) {
    const uint16_t total_length =
        (uint16_t)(unican_tx.length + UNICAN_CRC_LENGTH);

    unican_prepare_common_tx_frame(frame, false);

    frame->dlc = 6U;
    frame->data[0] =
        (uint8_t)(UNICAN_START_LONG_MESSAGE & 0x00FFU);
    frame->data[1] =
        (uint8_t)((UNICAN_START_LONG_MESSAGE >> 8U) & 0x00FFU);
    frame->data[2] =
        (uint8_t)(unican_tx.message_id & 0x00FFU);
    frame->data[3] =
        (uint8_t)((unican_tx.message_id >> 8U) & 0x00FFU);
    frame->data[4] =
        (uint8_t)(total_length & 0x00FFU);
    frame->data[5] =
        (uint8_t)((total_length >> 8U) & 0x00FFU);
}

static uint8_t unican_tx_stream_byte(uint16_t position) {
    if (position < unican_tx.length) {
        return unican_tx.data[position];
    }

    if (position == unican_tx.length) {
        return (uint8_t)(unican_tx.crc & 0x00FFU);
    }

    return (uint8_t)((unican_tx.crc >> 8U) & 0x00FFU);
}

static void unican_prepare_long_data_tx_frame(CanFrame* frame) {
    const uint16_t total_length =
        (uint16_t)(unican_tx.length + UNICAN_CRC_LENGTH);

    uint16_t remaining;
    uint16_t bytes_to_send;
    uint16_t index;

    unican_prepare_common_tx_frame(frame, true);

    remaining = (uint16_t)(total_length - unican_tx.stream_position);

    bytes_to_send = remaining;
    if (bytes_to_send > CAN_FRAME_MAX_DATA_SIZE) {
        bytes_to_send = CAN_FRAME_MAX_DATA_SIZE;
    }

    frame->dlc = (uint8_t)bytes_to_send;

    for (index = 0U; index < bytes_to_send; ++index) {
        frame->data[index] =
            unican_tx_stream_byte((uint16_t)(unican_tx.stream_position +
                                      index));
    }
}

static void unican_tx_fail(uint16_t error) {
    ++unican_status.tx_transport_errors;
    ++unican_status.tx_messages_failed;

    unican_record_error(error);
    unican_tx_clear();
}

static void unican_tx_complete_message(void) {
    ++unican_status.tx_messages_ok;
    unican_tx_clear();
}

static bool unican_tx_is_waiting_for_frame_result(void) {
    return (unican_tx.phase == UNICAN_TX_PHASE_SHORT_WAIT) ||
        (unican_tx.phase == UNICAN_TX_PHASE_LONG_START_WAIT) ||
        (unican_tx.phase == UNICAN_TX_PHASE_LONG_DATA_WAIT);
}

static void unican_check_tx_timeout(uint32_t now_ms) {
    uint32_t elapsed_ms;

    if (!unican_tx.active ||
        unican_tx.abort_in_progress ||
        !unican_tx_is_waiting_for_frame_result()) {
        return;
    }

    elapsed_ms = now_ms - unican_tx.pending_since_ms;

    if (elapsed_ms < UNICAN_TX_FRAME_TIMEOUT_MS) {
        return;
    }

    unican_tx.abort_in_progress = true;

    ++unican_status.tx_timeout_messages;
    unican_record_error(UNICAN_TX_TIMEOUT);

    unican_port_abort_tx();
}

static void unican_process_tx_events(void) {
    Can1TxEvent event;
    BoardStatus status;

    while (true) {
        status = unican_port_take_tx_event(&event);

        if (status == BOARD_ERR_NOT_READY) {
            return;
        }

        if (status != BOARD_OK) {
            unican_tx_fail(UNICAN_HW_ERROR);
            return;
        }

        /*
         * Сейчас tracked CAN TX используется только UniCAN.
         */
        if ((!unican_tx.active) ||
            (event.sequence != unican_tx.pending_sequence)) {
            ++unican_status.tx_transport_errors;
            unican_record_error(UNICAN_HW_ERROR);
            continue;
        }

        if (event.result != CAN1_TX_RESULT_OK) {
            if (unican_tx.abort_in_progress) {
                unican_tx_fail(UNICAN_TX_TIMEOUT);
            } else {
                unican_tx_fail(UNICAN_HW_ERROR);
            }

            return;
        }

        unican_tx.abort_in_progress = false;
        unican_tx.pending_sequence = 0U;
        unican_tx.pending_since_ms = 0U;

        if (unican_tx.phase == UNICAN_TX_PHASE_SHORT_WAIT) {
            unican_tx_complete_message();
            return;
        }

        if (unican_tx.phase == UNICAN_TX_PHASE_LONG_START_WAIT) {
            unican_tx.phase = UNICAN_TX_PHASE_LONG_DATA_SEND;
            return;
        }

        if (unican_tx.phase == UNICAN_TX_PHASE_LONG_DATA_WAIT) {
            const uint16_t total_length =
                (uint16_t)(unican_tx.length + UNICAN_CRC_LENGTH);

            unican_tx.stream_position =
                (uint16_t)(unican_tx.stream_position +
                    unican_tx.pending_data_length);

            unican_tx.pending_data_length = 0U;

            if (unican_tx.stream_position >= total_length) {
                unican_tx_complete_message();
            } else {
                unican_tx.phase = UNICAN_TX_PHASE_LONG_DATA_SEND;
            }

            return;
        }

        unican_tx_fail(UNICAN_HW_ERROR);
        return;
    }
}

static void unican_process_tx(uint32_t now_ms) {
    CanFrame frame;
    BoardStatus status;
    uint32_t sequence;

    unican_process_tx_events();

    if (!unican_tx.active) {
        return;
    }

    unican_check_tx_timeout(now_ms);

    if ((!unican_tx.active) || unican_tx.abort_in_progress) {
        return;
    }

    if (unican_tx.phase == UNICAN_TX_PHASE_SHORT_SEND) {
        unican_prepare_short_tx_frame(&frame);

        status = unican_port_send_frame_tracked(&frame, &sequence);

        if (status == BOARD_ERR_BUSY) {
            return;
        }

        if (status != BOARD_OK) {
            unican_tx_fail(UNICAN_HW_ERROR);
            return;
        }

        unican_tx.pending_sequence = sequence;
        unican_tx.pending_since_ms = now_ms;
        unican_tx.phase = UNICAN_TX_PHASE_SHORT_WAIT;

        return;
    }

    if (unican_tx.phase == UNICAN_TX_PHASE_LONG_START_SEND) {
        unican_prepare_long_start_tx_frame(&frame);

        status = unican_port_send_frame_tracked(&frame, &sequence);

        if (status == BOARD_ERR_BUSY) {
            return;
        }

        if (status != BOARD_OK) {
            unican_tx_fail(UNICAN_HW_ERROR);
            return;
        }

        unican_tx.pending_sequence = sequence;
        unican_tx.pending_since_ms = now_ms;
        unican_tx.phase = UNICAN_TX_PHASE_LONG_START_WAIT;

        return;
    }

    if (unican_tx.phase == UNICAN_TX_PHASE_LONG_DATA_SEND) {
        unican_prepare_long_data_tx_frame(&frame);

        status = unican_port_send_frame_tracked(&frame, &sequence);

        if (status == BOARD_ERR_BUSY) {
            return;
        }

        if (status != BOARD_OK) {
            unican_tx_fail(UNICAN_HW_ERROR);
            return;
        }

        unican_tx.pending_sequence = sequence;
        unican_tx.pending_since_ms = now_ms;
        unican_tx.pending_data_length = frame.dlc;
        unican_tx.phase = UNICAN_TX_PHASE_LONG_DATA_WAIT;
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

void unican_init(void) {
    memset(&unican_status, 0, sizeof(unican_status));
    memset(unican_slots, 0, sizeof(unican_slots));
    memset(unican_rx_buffers, 0, sizeof(unican_rx_buffers));
    memset(unican_ready_queue, 0, sizeof(unican_ready_queue));
    memset(&unican_tx, 0, sizeof(unican_tx));

    unican_ready_head = 0U;
    unican_ready_tail = 0U;
    unican_ready_count = 0U;

    unican_status.is_online = true;
    unican_status.last_error = UNICAN_OK;

    unican_status_recount();
}

void unican_close(void) {
    unican_status.is_online = false;

    memset(unican_slots, 0, sizeof(unican_slots));
    memset(unican_rx_buffers, 0, sizeof(unican_rx_buffers));
    memset(unican_ready_queue, 0, sizeof(unican_ready_queue));
    memset(&unican_tx, 0, sizeof(unican_tx));

    unican_ready_head = 0U;
    unican_ready_tail = 0U;
    unican_ready_count = 0U;

    unican_status_recount();
}

void unican_poll(uint32_t now_ms) {
    CanFrame frame;
    BoardStatus status;

    if (!unican_status.is_online) {
        return;
    }

    while (true) {
        status = unican_port_receive_frame(&frame);

        if (status == BOARD_ERR_NOT_READY) {
            break;
        }

        if (status != BOARD_OK) {
            ++unican_status.rx_transport_errors;
            unican_record_error(UNICAN_HW_ERROR);
            break;
        }

        unican_process_received_frame(&frame, now_ms);
    }

    unican_check_rx_timeouts(now_ms);
    unican_process_tx(now_ms);
}

BoardStatus unican_send(const UnicanMessage* message) {
    if (message == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!unican_status.is_online) {
        unican_record_error(UNICAN_OFFLINE);
        return BOARD_ERR_NOT_READY;
    }

    if (message->length > UNICAN_MAX_MESSAGE_DATA) {
        unican_record_error(UNICAN_MESSAGE_TOO_LONG);
        return BOARD_ERR_INVALID_ARG;
    }

    if ((message->data == NULL) && (message->length != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (unican_tx.active) {
        return BOARD_ERR_BUSY;
    }

    unican_tx.active = true;
    unican_tx.abort_in_progress = false;
    unican_tx.message_id = message->message_id;
    unican_tx.address_from = message->address_from;
    unican_tx.address_to = message->address_to;
    unican_tx.length = message->length;
    unican_tx.stream_position = 0U;

    if (message->length > 0U) {
        memcpy(unican_tx.data, message->data, message->length);
    }

    unican_tx.pending_sequence = 0U;
    unican_tx.pending_data_length = 0U;
    unican_tx.pending_since_ms = 0U;

    if (message->length <= UNICAN_SHORT_MESSAGE_MAX_DATA) {
        unican_tx.phase = UNICAN_TX_PHASE_SHORT_SEND;
        unican_tx.crc = 0U;
    } else {
        unican_tx.phase = UNICAN_TX_PHASE_LONG_START_SEND;
        unican_tx.crc =
            unican_crc16_xmodem(unican_tx.data, unican_tx.length);
    }

    unican_status_recount();

    return BOARD_OK;
}

BoardStatus unican_receive(UnicanMessage* message,
                           uint8_t* buffer,
                           uint16_t buffer_capacity) {
    uint8_t slot_index;
    UnicanSlot* slot;

    if (message == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!unican_status.is_online) {
        return BOARD_ERR_NOT_READY;
    }

    if (!unican_ready_peek(&slot_index)) {
        return BOARD_ERR_NOT_READY;
    }

    slot = &unican_slots[slot_index];

    if ((slot->length > 0U) && (buffer == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (buffer_capacity < slot->length) {
        return BOARD_ERR_INVALID_ARG;
    }

    message->message_id = slot->message_id;
    message->address_from = slot->address_from;
    message->address_to = slot->address_to;
    message->length = slot->length;
    message->data = buffer;

    if (slot->length > 0U) {
        memcpy(buffer, slot->data, slot->length);
    }

    unican_ready_pop();
    unican_slot_free(slot_index);

    return BOARD_OK;
}

bool unican_tx_is_busy(void) {
    return unican_tx.active;
}

void unican_get_status(UnicanStatus* status) {
    if (status == NULL) {
        return;
    }

    unican_status_recount();
    *status = unican_status;
}
