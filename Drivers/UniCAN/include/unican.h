#ifndef UNICAN_H
#define UNICAN_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"
#include "unican_config.h"
#include "unican_error.h"
#include "unican_msg_id.h"
#include "unican_node_addr.h"

typedef struct {
    uint16_t message_id;
    uint16_t address_from;
    uint16_t address_to;
    uint16_t length;

    const uint8_t *data;
} UnicanMessage;

typedef struct {
    bool is_online;
    bool tx_busy;

    uint16_t free_slots_count;
    uint16_t ready_messages_count;
    uint16_t active_rx_buffers_count;

    uint16_t last_error;

    uint32_t rx_messages_ok;
    uint32_t tx_messages_ok;
    uint32_t tx_messages_failed;
    uint32_t rx_transport_errors;
    uint32_t tx_transport_errors;
    uint32_t dropped_messages;
    uint32_t timeout_messages;
    uint32_t tx_timeout_messages;
} UnicanStatus;

void unican_init(void);

void unican_close(void);

void unican_poll(uint32_t now_ms);

BoardStatus unican_send(const UnicanMessage *message);

BoardStatus unican_receive(UnicanMessage *message,
                           uint8_t *buffer,
                           uint16_t buffer_capacity);

bool unican_tx_is_busy(void);

void unican_get_status(UnicanStatus *status);

#endif /* UNICAN_H */