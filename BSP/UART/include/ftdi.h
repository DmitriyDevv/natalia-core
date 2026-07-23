#ifndef NATALIA_FTDI_H
#define NATALIA_FTDI_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    FTDI_MODE_LOG = 0,
    FTDI_MODE_DATA = 1
} FtdiMode;

BoardStatus ftdi_init(void);

void ftdi_set_mode(FtdiMode mode);

FtdiMode ftdi_get_mode(void);

BoardStatus ftdi_write(const uint8_t *data, size_t size, size_t *accepted);

BoardStatus ftdi_is_tx_idle(uint8_t *idle);

BoardStatus ftdi_flush(uint32_t timeout_ms);

uint8_t ftdi_power_present(void);

uint32_t ftdi_log_dropped_count(void);

#endif /* NATALIA_FTDI_H */
