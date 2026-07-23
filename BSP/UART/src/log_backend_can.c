#include "log_backend.h"

#include "can1.h"
#include "timebase.h"

#define LOG_BACKEND_CAN_DEBUG_ID (0x7DBU)
#define LOG_BACKEND_CAN_TX_RETRY (200000UL)

/*
 * Pace frames so the receiver (a CAN -> slow-UART bridge with a shallow RX FIFO)
 * can drain each frame before the next arrives, otherwise it overflows and drops
 * the middle of each log line.
 */
#define LOG_BACKEND_CAN_FRAME_GAP_MS (1UL)

BoardStatus log_backend_init(void) {
    return can1_init();
}

void log_backend_write(const uint8_t *data, size_t size) {
    size_t offset = 0U;

    while (offset < size) {
        CanFrame frame;
        size_t chunk = size - offset;
        size_t i;

        if (chunk > CAN_FRAME_MAX_DATA_SIZE) {
            chunk = CAN_FRAME_MAX_DATA_SIZE;
        }

        frame.identifier = LOG_BACKEND_CAN_DEBUG_ID;
        frame.id_type = CAN_FRAME_STANDARD_ID;
        frame.frame_type = CAN_FRAME_DATA;
        frame.dlc = (uint8_t)chunk;

        for (i = 0U; i < chunk; ++i) {
            frame.data[i] = data[offset + i];
        }
        for (i = chunk; i < CAN_FRAME_MAX_DATA_SIZE; ++i) {
            frame.data[i] = 0U;
        }

        {
            uint32_t guard = 0U;

            while (can1_send(&frame) == BOARD_ERR_BUSY) {
                if (++guard >= LOG_BACKEND_CAN_TX_RETRY) {
                    break;
                }
            }
        }

        timebase_delay_ms_blocking(LOG_BACKEND_CAN_FRAME_GAP_MS);

        offset += chunk;
    }
}
