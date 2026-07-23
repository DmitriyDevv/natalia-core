#ifndef NATALIA_LOG_BACKEND_H
#define NATALIA_LOG_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

#define NATALIA_LOG_BACKEND_NONE    0
#define NATALIA_LOG_BACKEND_LPUART1 1
#define NATALIA_LOG_BACKEND_USART2  2
#define NATALIA_LOG_BACKEND_FTDI    3
#define NATALIA_LOG_BACKEND_CAN     4

#ifndef NATALIA_LOG_BACKEND
#define NATALIA_LOG_BACKEND NATALIA_LOG_BACKEND_NONE
#endif

#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)

BoardStatus log_backend_init(void);

void log_backend_write(const uint8_t *data, size_t size);

#endif

#endif /* NATALIA_LOG_BACKEND_H */
