#include "detector_log.h"

#include <stddef.h>
#include <stdint.h>

#include "debug_log.h"
#include "timebase.h"

static char detector_log_hex_digit(uint8_t value) {
    value &= 0x0FU;

    if (value < 10U) {
        return (char)('0' + value);
    }

    return (char)('A' + (value - 10U));
}

static void detector_log_hex8(uint8_t value) {
    char text[3];

    text[0] = detector_log_hex_digit((uint8_t)(value >> 4U));
    text[1] = detector_log_hex_digit(value);
    text[2] = '\0';

    debug_log_write(text);
}

static void detector_log_hex16(uint16_t value) {
    detector_log_hex8((uint8_t)(value >> 8U));
    detector_log_hex8((uint8_t)value);
}

static void detector_log_payload(const uint8_t* data, uint16_t length) {
    uint16_t index;

    if ((data == NULL) && (length != 0U)) {
        return;
    }

    for (index = 0U; index < length; ++index) {
        detector_log_hex8(data[index]);
    }
}

static void detector_log_message(const char* direction,
                                 const UnicanMessage* message) {
    if ((direction == NULL) || (message == NULL)) {
        return;
    }

    debug_log_write("DSTLOG|v=1|src=detector|ts=");
    debug_log_write_u32_inline(timebase_millis());

    debug_log_write("|dir=");
    debug_log_write(direction);

    debug_log_write("|id=");
    detector_log_hex16(message->message_id);

    debug_log_write("|data=");
    detector_log_payload(message->data, message->length);

    debug_log_write("\n");
}

void detector_log_rx(const UnicanMessage* message) {
    detector_log_message("rx", message);
}

void detector_log_tx(const UnicanMessage* message) {
    detector_log_message("tx", message);
}
