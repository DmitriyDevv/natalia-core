#include "debug_log.h"

#include "log_backend.h"

#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)

#include <stddef.h>

#define DEBUG_LOG_CHUNK_SIZE 32U

static void debug_log_flush_chunk(uint8_t *chunk, size_t *length) {
    if (*length > 0U) {
        log_backend_write(chunk, *length);
        *length = 0U;
    }
}

static void debug_log_put(uint8_t *chunk, size_t *length, uint8_t byte) {
    if (*length >= DEBUG_LOG_CHUNK_SIZE) {
        debug_log_flush_chunk(chunk, length);
    }

    chunk[*length] = byte;
    ++(*length);
}

static void debug_log_write_number(uint8_t *chunk, size_t *length, uint32_t value) {
    char buffer[10];
    uint32_t index = 0U;

    if (value == 0U) {
        debug_log_put(chunk, length, (uint8_t)'0');
        return;
    }

    while ((value != 0U) && (index < (uint32_t)sizeof(buffer))) {
        buffer[index] = (char)('0' + (value % 10U));
        value /= 10U;
        ++index;
    }

    while (index > 0U) {
        --index;
        debug_log_put(chunk, length, (uint8_t)buffer[index]);
    }
}

#endif /* NATALIA_LOG_BACKEND != NONE */

BoardStatus debug_log_init(void) {
#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)
    return log_backend_init();
#else
    return BOARD_OK;
#endif
}

void debug_log_write(const char *string) {
#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)
    uint8_t chunk[DEBUG_LOG_CHUNK_SIZE];
    size_t length = 0U;

    if (string == NULL) {
        return;
    }

    while (*string != '\0') {
        if (*string == '\n') {
            debug_log_put(chunk, &length, (uint8_t)'\r');
        }

        debug_log_put(chunk, &length, (uint8_t)*string);
        ++string;
    }

    debug_log_flush_chunk(chunk, &length);
#else
    (void)string;
#endif
}

void debug_log_write_u32_inline(uint32_t value) {
#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)
    uint8_t chunk[DEBUG_LOG_CHUNK_SIZE];
    size_t length = 0U;

    debug_log_write_number(chunk, &length, value);
    debug_log_flush_chunk(chunk, &length);
#else
    (void)value;
#endif
}

void debug_log_write_u32(const char *prefix, uint32_t value) {
#if (NATALIA_LOG_BACKEND != NATALIA_LOG_BACKEND_NONE)
    debug_log_write(prefix);
    debug_log_write_u32_inline(value);
    debug_log_write("\n");
#else
    (void)prefix;
    (void)value;
#endif
}
