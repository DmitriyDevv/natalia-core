#include "can1.h"

#include <stddef.h>
#include <stdint.h>

#include "can1_private.h"
#include "stm32l496xx.h"

#define CAN1_RX_QUEUE_LENGTH (32U)
#define CAN1_TX_QUEUE_LENGTH (32U)
#define CAN1_TX_EVENT_QUEUE_LENGTH (32U)

static CanFrame can1_rx_queue[CAN1_RX_QUEUE_LENGTH];
static volatile uint16_t can1_rx_head = 0U;
static volatile uint16_t can1_rx_tail = 0U;
static volatile uint16_t can1_rx_count = 0U;


typedef struct {
    CanFrame frame;
    uint32_t sequence;
    bool tracked;
} Can1TxRequest;

static Can1TxRequest can1_tx_queue[CAN1_TX_QUEUE_LENGTH];
static volatile uint16_t can1_tx_head = 0U;
static volatile uint16_t can1_tx_tail = 0U;
static volatile uint16_t can1_tx_count = 0U;

static Can1TxEvent can1_tx_event_queue[CAN1_TX_EVENT_QUEUE_LENGTH];
static volatile uint16_t can1_tx_event_head = 0U;
static volatile uint16_t can1_tx_event_tail = 0U;
static volatile uint16_t can1_tx_event_count = 0U;

static uint32_t can1_next_tx_sequence = 1U;

static bool can1_mailbox_tracked[3U] = {false, false, false};
static bool can1_mailbox_abort_requested[3U] = {false, false, false};
static uint32_t can1_mailbox_sequence[3U] = {0U, 0U, 0U};

static volatile Can1Stats can1_stats = {0};

void CAN1_TX_IRQHandler(void);
void CAN1_RX0_IRQHandler(void);
void CAN1_SCE_IRQHandler(void);

static uint32_t can1_enter_critical(void) {
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();

    return primask;
}

static void can1_leave_critical(uint32_t primask) {
    if (primask == 0U) {
        __enable_irq();
    }
}

static bool can1_frame_is_valid(const CanFrame* frame) {
    if (frame == NULL) {
        return false;
    }

    if (frame->dlc > CAN_FRAME_MAX_DATA_SIZE) {
        return false;
    }

    if ((frame->id_type != CAN_FRAME_STANDARD_ID) &&
        (frame->id_type != CAN_FRAME_EXTENDED_ID)) {
        return false;
    }

    if ((frame->frame_type != CAN_FRAME_DATA) &&
        (frame->frame_type != CAN_FRAME_REMOTE)) {
        return false;
    }

    if ((frame->id_type == CAN_FRAME_STANDARD_ID) &&
        (frame->identifier > 0x7FFUL)) {
        return false;
    }

    if ((frame->id_type == CAN_FRAME_EXTENDED_ID) &&
        (frame->identifier > 0x1FFFFFFFUL)) {
        return false;
    }

    return true;
}

static uint16_t can1_next_index(uint16_t index, uint16_t length) {
    ++index;

    if (index >= length) {
        index = 0U;
    }

    return index;
}

static uint32_t can1_allocate_tx_sequence(void) {
    uint32_t sequence = can1_next_tx_sequence;

    ++can1_next_tx_sequence;

    if (can1_next_tx_sequence == 0U) {
        can1_next_tx_sequence = 1U;
    }

    return sequence;
}

static bool can1_push_tx_event(const Can1TxEvent* event) {
    if (can1_tx_event_count >= CAN1_TX_EVENT_QUEUE_LENGTH) {
        ++can1_stats.tx_events_dropped;
        return false;
    }

    can1_tx_event_queue[can1_tx_event_head] = *event;
    can1_tx_event_head =
        can1_next_index(can1_tx_event_head, CAN1_TX_EVENT_QUEUE_LENGTH);
    ++can1_tx_event_count;

    return true;
}

static void can1_reset_queues_and_stats(void) {
    uint32_t primask = can1_enter_critical();

    can1_rx_head = 0U;
    can1_rx_tail = 0U;
    can1_rx_count = 0U;

    can1_tx_head = 0U;
    can1_tx_tail = 0U;
    can1_tx_count = 0U;

    can1_tx_event_head = 0U;
    can1_tx_event_tail = 0U;
    can1_tx_event_count = 0U;

    can1_next_tx_sequence = 1U;

    can1_mailbox_tracked[0U] = false;
    can1_mailbox_tracked[1U] = false;
    can1_mailbox_tracked[2U] = false;

    can1_mailbox_abort_requested[0U] = false;
    can1_mailbox_abort_requested[1U] = false;
    can1_mailbox_abort_requested[2U] = false;

    can1_mailbox_sequence[0U] = 0U;
    can1_mailbox_sequence[1U] = 0U;
    can1_mailbox_sequence[2U] = 0U;

    can1_stats.rx_received = 0U;
    can1_stats.rx_dropped = 0U;
    can1_stats.tx_queued = 0U;
    can1_stats.tx_completed = 0U;
    can1_stats.tx_failed = 0U;
    can1_stats.tx_events_dropped = 0U;
    can1_stats.error_interrupts = 0U;
    can1_stats.bus_off_events = 0U;
    can1_stats.error_passive_events = 0U;
    can1_stats.error_warning_events = 0U;

    can1_leave_critical(primask);
}

static void can1_unpack_rx_mailbox(CanFrame* frame) {
    const uint32_t rir = CAN1->sFIFOMailBox[0].RIR;
    const uint32_t rdtr = CAN1->sFIFOMailBox[0].RDTR;
    const uint32_t rdlr = CAN1->sFIFOMailBox[0].RDLR;
    const uint32_t rdhr = CAN1->sFIFOMailBox[0].RDHR;
    uint8_t dlc;

    dlc = (uint8_t)(rdtr & 0x0FUL);
    if (dlc > CAN_FRAME_MAX_DATA_SIZE) {
        dlc = CAN_FRAME_MAX_DATA_SIZE;
    }

    frame->dlc = dlc;
    frame->frame_type = ((rir & CAN_RI0R_RTR) != 0U)
                            ? CAN_FRAME_REMOTE
                            : CAN_FRAME_DATA;

    if ((rir & CAN_RI0R_IDE) != 0U) {
        frame->id_type = CAN_FRAME_EXTENDED_ID;
        frame->identifier = (rir >> 3U) & 0x1FFFFFFFUL;
    } else {
        frame->id_type = CAN_FRAME_STANDARD_ID;
        frame->identifier = (rir >> 21U) & 0x7FFUL;
    }

    frame->data[0] = (uint8_t)rdlr;
    frame->data[1] = (uint8_t)(rdlr >> 8U);
    frame->data[2] = (uint8_t)(rdlr >> 16U);
    frame->data[3] = (uint8_t)(rdlr >> 24U);

    frame->data[4] = (uint8_t)rdhr;
    frame->data[5] = (uint8_t)(rdhr >> 8U);
    frame->data[6] = (uint8_t)(rdhr >> 16U);
    frame->data[7] = (uint8_t)(rdhr >> 24U);
}

static bool can1_push_rx_from_isr(const CanFrame* frame) {
    if (can1_rx_count >= CAN1_RX_QUEUE_LENGTH) {
        ++can1_stats.rx_dropped;
        return false;
    }

    can1_rx_queue[can1_rx_head] = *frame;
    can1_rx_head = can1_next_index(can1_rx_head, CAN1_RX_QUEUE_LENGTH);
    ++can1_rx_count;
    ++can1_stats.rx_received;

    return true;
}

static bool can1_select_empty_mailbox(uint32_t* mailbox) {
    const uint32_t tsr = CAN1->TSR;

    if ((tsr & CAN_TSR_TME0) != 0U) {
        *mailbox = 0U;
        return true;
    }

    if ((tsr & CAN_TSR_TME1) != 0U) {
        *mailbox = 1U;
        return true;
    }

    if ((tsr & CAN_TSR_TME2) != 0U) {
        *mailbox = 2U;
        return true;
    }

    return false;
}

static void can1_load_tx_mailbox(uint32_t mailbox, const Can1TxRequest* request) {
    const CanFrame* frame = &request->frame;
    uint32_t tir = 0U;
    uint32_t tdlr;
    uint32_t tdhr;

    if (frame->id_type == CAN_FRAME_EXTENDED_ID) {
        tir = ((frame->identifier & 0x1FFFFFFFUL) << 3U) |
            CAN_TI0R_IDE;
    } else {
        tir = ((frame->identifier & 0x7FFUL) << 21U);
    }

    if (frame->frame_type == CAN_FRAME_REMOTE) {
        tir |= CAN_TI0R_RTR;
    }

    tdlr = ((uint32_t)frame->data[0]) |
        ((uint32_t)frame->data[1] << 8U) |
        ((uint32_t)frame->data[2] << 16U) |
        ((uint32_t)frame->data[3] << 24U);

    tdhr = ((uint32_t)frame->data[4]) |
        ((uint32_t)frame->data[5] << 8U) |
        ((uint32_t)frame->data[6] << 16U) |
        ((uint32_t)frame->data[7] << 24U);

    can1_mailbox_tracked[mailbox] = request->tracked;
    can1_mailbox_abort_requested[mailbox] = false;
    can1_mailbox_sequence[mailbox] = request->sequence;

    CAN1->sTxMailBox[mailbox].TDTR = (uint32_t)(frame->dlc & 0x0FU);
    CAN1->sTxMailBox[mailbox].TDLR = tdlr;
    CAN1->sTxMailBox[mailbox].TDHR = tdhr;

    CAN1->sTxMailBox[mailbox].TIR = tir | CAN_TI0R_TXRQ;
}

static void can1_kick_tx_locked(void) {
    uint32_t mailbox;

    while ((can1_tx_count > 0U) &&
        can1_select_empty_mailbox(&mailbox)) {
        const Can1TxRequest* request = &can1_tx_queue[can1_tx_tail];

        can1_load_tx_mailbox(mailbox, request);

        can1_tx_tail = can1_next_index(can1_tx_tail,
                                       CAN1_TX_QUEUE_LENGTH);
        --can1_tx_count;
    }
}

static void can1_process_tx_completion(uint32_t tsr,
                                       uint32_t mailbox,
                                       uint32_t rqcp_mask,
                                       uint32_t txok_mask) {
    Can1TxEvent event;

    if ((tsr & rqcp_mask) == 0U) {
        return;
    }

    if ((tsr & txok_mask) != 0U) {
        ++can1_stats.tx_completed;
    } else {
        ++can1_stats.tx_failed;
    }

    if (can1_mailbox_tracked[mailbox]) {
        event.sequence = can1_mailbox_sequence[mailbox];

        if (can1_mailbox_abort_requested[mailbox]) {
            event.result = CAN1_TX_RESULT_ABORTED;
        } else if ((tsr & txok_mask) != 0U) {
            event.result = CAN1_TX_RESULT_OK;
        } else {
            event.result = CAN1_TX_RESULT_FAILED;
        }

        (void)can1_push_tx_event(&event);
    }

    can1_mailbox_tracked[mailbox] = false;
    can1_mailbox_abort_requested[mailbox] = false;
    can1_mailbox_sequence[mailbox] = 0U;

    CAN1->TSR = rqcp_mask;
}

BoardStatus can1_init(void) {
    BoardStatus status;

    can1_reset_queues_and_stats();

    status = can1_configure_hardware();
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}

static BoardStatus can1_enqueue_tx(const CanFrame* frame,
                                   bool tracked,
                                   uint32_t* sequence) {
    uint32_t primask;
    uint32_t assigned_sequence = 0U;

    if (!can1_frame_is_valid(frame)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (tracked && (sequence == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = can1_enter_critical();

    if (can1_tx_count >= CAN1_TX_QUEUE_LENGTH) {
        can1_leave_critical(primask);
        return BOARD_ERR_BUSY;
    }

    if (tracked) {
        assigned_sequence = can1_allocate_tx_sequence();
    }

    can1_tx_queue[can1_tx_head].frame = *frame;
    can1_tx_queue[can1_tx_head].tracked = tracked;
    can1_tx_queue[can1_tx_head].sequence = assigned_sequence;

    can1_tx_head = can1_next_index(can1_tx_head, CAN1_TX_QUEUE_LENGTH);
    ++can1_tx_count;
    ++can1_stats.tx_queued;

    can1_kick_tx_locked();

    can1_leave_critical(primask);

    if (tracked) {
        *sequence = assigned_sequence;
    }

    return BOARD_OK;
}

BoardStatus can1_send(const CanFrame* frame) {
    return can1_enqueue_tx(frame, false, NULL);
}

BoardStatus can1_send_tracked(const CanFrame* frame, uint32_t* sequence) {
    return can1_enqueue_tx(frame, true, sequence);
}

BoardStatus can1_take_tx_event(Can1TxEvent* event) {
    uint32_t primask;

    if (event == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = can1_enter_critical();

    if (can1_tx_event_count == 0U) {
        can1_leave_critical(primask);
        return BOARD_ERR_NOT_READY;
    }

    *event = can1_tx_event_queue[can1_tx_event_tail];

    can1_tx_event_tail =
        can1_next_index(can1_tx_event_tail, CAN1_TX_EVENT_QUEUE_LENGTH);
    --can1_tx_event_count;

    can1_leave_critical(primask);

    return BOARD_OK;
}

BoardStatus can1_receive(CanFrame* frame) {
    uint32_t primask;

    if (frame == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    primask = can1_enter_critical();

    if (can1_rx_count == 0U) {
        can1_leave_critical(primask);
        return BOARD_ERR_NOT_READY;
    }

    *frame = can1_rx_queue[can1_rx_tail];
    can1_rx_tail = can1_next_index(can1_rx_tail, CAN1_RX_QUEUE_LENGTH);
    --can1_rx_count;

    can1_leave_critical(primask);

    return BOARD_OK;
}

void can1_abort_all_tx(void) {
    uint32_t primask = can1_enter_critical();
    uint16_t index;

    while (can1_tx_count > 0U) {
        const Can1TxRequest* request = &can1_tx_queue[can1_tx_tail];

        if (request->tracked) {
            const Can1TxEvent event = {
                .sequence = request->sequence,
                .result = CAN1_TX_RESULT_ABORTED
            };

            (void)can1_push_tx_event(&event);
        }

        ++can1_stats.tx_failed;

        can1_tx_tail =
            can1_next_index(can1_tx_tail, CAN1_TX_QUEUE_LENGTH);
        --can1_tx_count;
    }

    can1_tx_head = can1_tx_tail;

    for (index = 0U; index < 3U; ++index) {
        if (can1_mailbox_tracked[index]) {
            can1_mailbox_abort_requested[index] = true;
        }
    }

    CAN1->TSR |= CAN_TSR_ABRQ0 |
        CAN_TSR_ABRQ1 |
        CAN_TSR_ABRQ2;

    can1_leave_critical(primask);
}

void can1_get_stats(Can1Stats* stats) {
    uint32_t primask;

    if (stats == NULL) {
        return;
    }

    primask = can1_enter_critical();

    stats->rx_received = can1_stats.rx_received;
    stats->rx_dropped = can1_stats.rx_dropped;
    stats->tx_queued = can1_stats.tx_queued;
    stats->tx_completed = can1_stats.tx_completed;
    stats->tx_failed = can1_stats.tx_failed;
    stats->tx_events_dropped = can1_stats.tx_events_dropped;
    stats->error_interrupts = can1_stats.error_interrupts;
    stats->bus_off_events = can1_stats.bus_off_events;
    stats->error_passive_events = can1_stats.error_passive_events;
    stats->error_warning_events = can1_stats.error_warning_events;

    can1_leave_critical(primask);
}

void CAN1_RX0_IRQHandler(void) {
    while ((CAN1->RF0R & CAN_RF0R_FMP0_Msk) != 0U) {
        CanFrame frame;

        can1_unpack_rx_mailbox(&frame);
        (void)can1_push_rx_from_isr(&frame);

        CAN1->RF0R |= CAN_RF0R_RFOM0;
    }

    if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
        CAN1->RF0R |= CAN_RF0R_FOVR0;
        ++can1_stats.rx_dropped;
    }

    if ((CAN1->RF0R & CAN_RF0R_FULL0) != 0U) {
        CAN1->RF0R |= CAN_RF0R_FULL0;
    }
}

void CAN1_TX_IRQHandler(void) {
    const uint32_t tsr = CAN1->TSR;

    can1_process_tx_completion(tsr,
                               0U,
                               CAN_TSR_RQCP0,
                               CAN_TSR_TXOK0);

    can1_process_tx_completion(tsr,
                               1U,
                               CAN_TSR_RQCP1,
                               CAN_TSR_TXOK1);

    can1_process_tx_completion(tsr,
                               2U,
                               CAN_TSR_RQCP2,
                               CAN_TSR_TXOK2);

    can1_kick_tx_locked();
}

void CAN1_SCE_IRQHandler(void) {
    const uint32_t esr = CAN1->ESR;

    ++can1_stats.error_interrupts;

    if ((esr & CAN_ESR_BOFF) != 0U) {
        ++can1_stats.bus_off_events;
    }

    if ((esr & CAN_ESR_EPVF) != 0U) {
        ++can1_stats.error_passive_events;
    }

    if ((esr & CAN_ESR_EWGF) != 0U) {
        ++can1_stats.error_warning_events;
    }


    CAN1->ESR |= CAN_ESR_LEC;


    CAN1->MSR = CAN_MSR_ERRI;
}
