#ifndef NATALIA_USB_CDC_H
#define NATALIA_USB_CDC_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

BoardStatus usb_cdc_init(void);
BoardStatus usb_cdc_deinit(void);
BoardStatus usb_cdc_write(const void *buffer, size_t size, size_t *bytes_written);
BoardStatus usb_cdc_is_ready(uint8_t *is_ready);

#endif