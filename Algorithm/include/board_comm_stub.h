#ifndef NATALIA_CORE_BOARD_COMM_STUB_H
#define NATALIA_CORE_BOARD_COMM_STUB_H

#include <stdbool.h>
#include <stdint.h>

void board_comm_stub_reset(void);

void board_comm_stub_inject_rx(uint16_t message_id,
                               uint16_t address_from,
                               uint16_t address_to,
                               const uint8_t* data,
                               uint16_t length);

uint32_t board_comm_stub_tx_count(void);

bool board_comm_stub_last_tx(uint16_t* message_id,
                             uint16_t* address_to,
                             uint8_t* buffer,
                             uint16_t capacity,
                             uint16_t* length);

#endif /* NATALIA_CORE_BOARD_COMM_STUB_H */
