#include "crc16.h"

#define CRC16_CCITT_POLY 0x1021U
#define CRC16_CCITT_INIT 0xFFFFU

uint16_t crc16_ccitt(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = CRC16_CCITT_INIT;
    size_t i;

    if ((bytes == 0) && (size > 0U)) {
        return crc;
    }

    for (i = 0U; i < size; ++i) {
        uint8_t bit;

        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)bytes[i] << 8));

        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC16_CCITT_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}
