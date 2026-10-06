#include "log_backend.h"

#include "can1.h"
#include "stm32l4xx.h"
#include "timebase.h"

#define LOG_BACKEND_CAN_DEBUG_ID     (0x7DBU)
#define LOG_BACKEND_CAN_BUFFER_SIZE  (8192UL)
#define LOG_BACKEND_CAN_NOTICE_SIZE  (40U)

_Static_assert((LOG_BACKEND_CAN_BUFFER_SIZE & (LOG_BACKEND_CAN_BUFFER_SIZE - 1UL)) == 0UL,
               "log buffer size must be a power of two");

/*
 * Log text is queued here and sent from the 1 ms SysTick hook, one CAN frame per
 * tick: writing never blocks the caller, and the 1 ms gap between frames still
 * lets the receiver (a CAN -> slow-UART bridge with a shallow RX FIFO) drain each
 * frame before the next one. A chunk that does not fit is dropped whole and
 * reported by a "[log: N bytes dropped]" line once there is room again.
 */
static uint8_t log_backend_can_buffer[LOG_BACKEND_CAN_BUFFER_SIZE];
static volatile uint32_t log_backend_can_head;
static volatile uint32_t log_backend_can_tail;
static uint32_t log_backend_can_dropped;
static volatile uint8_t log_backend_can_ready;

static uint32_t log_backend_can_free(void) {
    return LOG_BACKEND_CAN_BUFFER_SIZE - (log_backend_can_head - log_backend_can_tail);
}

static void log_backend_can_copy_in(const uint8_t *data, size_t size) {
    uint32_t head = log_backend_can_head;
    size_t i;

    for (i = 0U; i < size; ++i) {
        log_backend_can_buffer[(head + (uint32_t)i) & (LOG_BACKEND_CAN_BUFFER_SIZE - 1UL)] = data[i];
    }

    log_backend_can_head = head + (uint32_t)size;
}

static size_t log_backend_can_build_notice(uint8_t *notice, uint32_t dropped) {
    static const char prefix[] = "\r\n[log: ";
    static const char suffix[] = " bytes dropped]\r\n";
    char digits[10];
    size_t length = 0U;
    size_t count = 0U;
    size_t i;

    for (i = 0U; prefix[i] != '\0'; ++i) {
        notice[length++] = (uint8_t)prefix[i];
    }

    do {
        digits[count++] = (char)('0' + (dropped % 10U));
        dropped /= 10U;
    } while ((dropped != 0U) && (count < sizeof(digits)));

    while (count > 0U) {
        notice[length++] = (uint8_t)digits[--count];
    }

    for (i = 0U; suffix[i] != '\0'; ++i) {
        notice[length++] = (uint8_t)suffix[i];
    }

    return length;
}

BoardStatus log_backend_init(void) {
    BoardStatus status;

    log_backend_can_head = 0U;
    log_backend_can_tail = 0U;
    log_backend_can_dropped = 0U;

    status = can1_init();
    log_backend_can_ready = (status == BOARD_OK) ? 1U : 0U;

    return status;
}

void log_backend_write(const uint8_t *data, size_t size) {
    uint8_t notice[LOG_BACKEND_CAN_NOTICE_SIZE];
    size_t notice_length = 0U;
    uint32_t primask;

    if ((data == NULL) || (size == 0U)) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (log_backend_can_dropped != 0U) {
        notice_length = log_backend_can_build_notice(notice, log_backend_can_dropped);
        if ((notice_length + size) <= log_backend_can_free()) {
            log_backend_can_copy_in(notice, notice_length);
            log_backend_can_dropped = 0U;
        }
    }

    if ((log_backend_can_dropped == 0U) && (size <= log_backend_can_free())) {
        log_backend_can_copy_in(data, size);
    } else {
        log_backend_can_dropped += (uint32_t)size;
    }

    if (primask == 0UL) {
        __enable_irq();
    }
}

void timebase_tick_hook(void) {
    CanFrame frame;
    uint32_t tail = log_backend_can_tail;
    uint32_t pending = log_backend_can_head - tail;
    uint32_t chunk;
    uint32_t i;

    if ((log_backend_can_ready == 0U) || (pending == 0U)) {
        return;
    }

    chunk = (pending > CAN_FRAME_MAX_DATA_SIZE) ? CAN_FRAME_MAX_DATA_SIZE : pending;

    frame.identifier = LOG_BACKEND_CAN_DEBUG_ID;
    frame.id_type = CAN_FRAME_STANDARD_ID;
    frame.frame_type = CAN_FRAME_DATA;
    frame.dlc = (uint8_t)chunk;

    for (i = 0U; i < CAN_FRAME_MAX_DATA_SIZE; ++i) {
        frame.data[i] = (i < chunk)
            ? log_backend_can_buffer[(tail + i) & (LOG_BACKEND_CAN_BUFFER_SIZE - 1UL)]
            : 0U;
    }

    if (can1_send(&frame) == BOARD_OK) {
        log_backend_can_tail = tail + chunk;
    }
}
