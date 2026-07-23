#ifndef NATALIA_CORE_CRC16_H
#define NATALIA_CORE_CRC16_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_ccitt(const void *data, size_t size);

#endif /* NATALIA_CORE_CRC16_H */
