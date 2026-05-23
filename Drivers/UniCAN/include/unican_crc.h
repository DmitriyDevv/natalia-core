#ifndef UNICAN_CRC_H
#define UNICAN_CRC_H

#include <stdint.h>

uint16_t unican_crc16_xmodem(const uint8_t *buffer, uint16_t length);

#endif /* UNICAN_CRC_H */