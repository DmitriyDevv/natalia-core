#include "log_backend.h"

#include "usart2.h"

#define LOG_BACKEND_USART2_BAUDRATE (115200UL)

BoardStatus log_backend_init(void) {
    return usart2_init(LOG_BACKEND_USART2_BAUDRATE);
}

void log_backend_write(const uint8_t *data, size_t size) {
    (void)usart2_write(data, size);
}
