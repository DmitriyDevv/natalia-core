#ifndef NATALIA_USART2_H
#define NATALIA_USART2_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

BoardStatus usart2_init(uint32_t baudrate);

BoardStatus usart2_write_byte(uint8_t byte);

BoardStatus usart2_write(const uint8_t *data, size_t size);

#endif /* NATALIA_USART2_H */
