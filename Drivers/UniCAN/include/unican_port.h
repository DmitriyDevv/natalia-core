#ifndef NATALIA_UNICAN_PORT_H
#define NATALIA_UNICAN_PORT_H

#include "can1.h"
#include "status.h"

BoardStatus unican_port_send_frame_tracked(const CanFrame *frame, uint32_t *sequence);

BoardStatus unican_port_take_tx_event(Can1TxEvent *event);

BoardStatus unican_port_receive_frame(CanFrame *frame);

void unican_port_abort_tx(void);

#endif /* NATALIA_UNICAN_PORT_H */