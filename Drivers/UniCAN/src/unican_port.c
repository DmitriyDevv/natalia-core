#include "unican_port.h"

BoardStatus unican_port_send_frame_tracked(const CanFrame* frame,
                                           uint32_t* sequence) {
    return can1_send_tracked(frame, sequence);
}

BoardStatus unican_port_take_tx_event(Can1TxEvent* event) {
    return can1_take_tx_event(event);
}

BoardStatus unican_port_receive_frame(CanFrame* frame) {
    return can1_receive(frame);
}

void unican_port_abort_tx(void) {
    can1_abort_all_tx();
}
