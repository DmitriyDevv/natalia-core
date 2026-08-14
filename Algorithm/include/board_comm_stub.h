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

/* Returns the most recently sent message. Kept for compatibility; equivalent to
 * board_comm_stub_tx_at(board_comm_stub_tx_count() - 1, ...). */
bool board_comm_stub_last_tx(uint16_t* message_id,
                             uint16_t* address_to,
                             uint8_t* buffer,
                             uint16_t capacity,
                             uint16_t* length);

/* Returns the successfully sent message with ordinal `index` (0 = first sent).
 * Fails if `index` was never sent or has already been overwritten in the TX
 * ring, which retains only the last 8 messages. */
bool board_comm_stub_tx_at(uint32_t index,
                           uint16_t* message_id,
                           uint16_t* address_to,
                           uint8_t* buffer,
                           uint16_t capacity,
                           uint16_t* length);

/* Arms the next `fail_count` transmit attempts to fail: board_comm_send accepts
 * the message but the transmission is reported as failed through
 * board_comm_get_status().tx_messages_failed (the asynchronous failure path the
 * transport retry logic consumes). Failed sends are not stored in the TX ring.
 * Reset by board_comm_stub_reset(). Defaults to no failures. */
void board_comm_stub_set_send_fail(uint32_t fail_count);

#endif /* NATALIA_CORE_BOARD_COMM_STUB_H */
