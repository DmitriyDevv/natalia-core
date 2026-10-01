#include "crc16.h"

uint16_t crc16_ccitt(const void *data, size_t size) {
    return crc16_ccitt_software(data, size);
}
