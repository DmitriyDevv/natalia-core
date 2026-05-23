#ifndef NATALIA_CAN1_H
#define NATALIA_CAN1_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

#define CAN_FRAME_MAX_DATA_SIZE (8U)

typedef enum {
    CAN_FRAME_STANDARD_ID = 0,
    CAN_FRAME_EXTENDED_ID = 1
} CanFrameIdType;

typedef enum {
    CAN_FRAME_DATA = 0,
    CAN_FRAME_REMOTE = 1
} CanFrameType;

typedef struct {
    uint32_t identifier;
    CanFrameIdType id_type;
    CanFrameType frame_type;
    uint8_t dlc;
    uint8_t data[CAN_FRAME_MAX_DATA_SIZE];
} CanFrame;

typedef struct {
    uint32_t rx_received;
    uint32_t rx_dropped;
    uint32_t tx_queued;
    uint32_t tx_completed;
    uint32_t tx_failed;
    uint32_t tx_events_dropped;
    uint32_t error_interrupts;
    uint32_t bus_off_events;
    uint32_t error_passive_events;
    uint32_t error_warning_events;
} Can1Stats;

typedef enum {
    CAN1_TX_RESULT_OK = 0,
    CAN1_TX_RESULT_FAILED,
    CAN1_TX_RESULT_ABORTED
} Can1TxResult;

typedef struct {
    uint32_t sequence;
    Can1TxResult result;
} Can1TxEvent;

BoardStatus can1_init(void);

BoardStatus can1_send(const CanFrame* frame);

BoardStatus can1_send_tracked(const CanFrame* frame, uint32_t* sequence);

BoardStatus can1_take_tx_event(Can1TxEvent* event);

BoardStatus can1_receive(CanFrame* frame);

void can1_abort_all_tx(void);

void can1_get_stats(Can1Stats* stats);

#endif /* NATALIA_CAN1_H */
