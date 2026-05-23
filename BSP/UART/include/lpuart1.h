#ifndef NATALIA_LPUART1_H
#define NATALIA_LPUART1_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

BoardStatus lpuart1_init(uint32_t baudrate);

BoardStatus lpuart1_write_byte(uint8_t byte);

BoardStatus lpuart1_write(const uint8_t *data, size_t size);

BoardStatus lpuart1_write_string(const char *string);

#endif /* NATALIA_LPUART1_H */