#include "debug_log.h"

#ifdef NATALIA_ENABLE_DEBUG_LOG
#include "lpuart1.h"
#endif

#define DEBUG_LOG_BAUDRATE (115200UL)

static void debug_log_write_number(uint32_t value) {
#ifdef NATALIA_ENABLE_DEBUG_LOG
    char buffer[10];
    uint32_t index = 0U;

    if (value == 0U) {
        (void)lpuart1_write_byte((uint8_t)'0');
        return;
    }

    while ((value != 0U) && (index < (uint32_t)sizeof(buffer))) {
        buffer[index] = (char)('0' + (value % 10U));
        value /= 10U;
        ++index;
    }

    while (index > 0U) {
        --index;
        (void)lpuart1_write_byte((uint8_t)buffer[index]);
    }
#else
    (void)value;
#endif
}

BoardStatus debug_log_init(void) {
#ifdef NATALIA_ENABLE_DEBUG_LOG
    return lpuart1_init(DEBUG_LOG_BAUDRATE);
#else
    return BOARD_OK;
#endif
}

void debug_log_write(const char* string) {
#ifdef NATALIA_ENABLE_DEBUG_LOG
    if (string != NULL) {
        (void)lpuart1_write_string(string);
    }
#else
    (void)string;
#endif
}

void debug_log_write_u32_inline(uint32_t value) {
#ifdef NATALIA_ENABLE_DEBUG_LOG
    debug_log_write_number(value);
#else
    (void)value;
#endif
}

void debug_log_write_u32(const char* prefix, uint32_t value) {
#ifdef NATALIA_ENABLE_DEBUG_LOG
    debug_log_write(prefix);
    debug_log_write_u32_inline(value);
    debug_log_write("\n");
#else
    (void)prefix;
    (void)value;
#endif
}
