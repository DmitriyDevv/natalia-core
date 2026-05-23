#ifndef NATALIA_DEBUG_LOG_H
#define NATALIA_DEBUG_LOG_H

#include <stdint.h>

#include "status.h"

BoardStatus debug_log_init(void);

void debug_log_write(const char *string);

void debug_log_write_u32_inline(uint32_t value);

void debug_log_write_u32(const char *prefix, uint32_t value);

#endif /* NATALIA_DEBUG_LOG_H */