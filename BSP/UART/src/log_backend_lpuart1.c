#include "log_backend.h"

#include "lpuart1.h"

#define LOG_BACKEND_LPUART1_BAUDRATE (115200UL)

BoardStatus log_backend_init(void) {
    return lpuart1_init(LOG_BACKEND_LPUART1_BAUDRATE);
}

void log_backend_write(const uint8_t *data, size_t size) {
    (void)lpuart1_write(data, size);
}
