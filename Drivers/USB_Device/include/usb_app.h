#ifndef USB_APP_H_
#define USB_APP_H_

#include <stdbool.h>
#include <stdint.h>

extern uint32_t usbdev_msec;

void usbdev_tick(void);

void USBapp_Init(void);
void USBapp_DeInit(void);
void USBapp_Poll(void);
uint8_t USBapp_CdcIsReady(void);

void vcom_write(uint8_t ch, const void *buffer, uint16_t size);
void vcom0_write(const void *buffer, uint16_t size);
bool vcom0_write_blocking(const void *buffer, uint16_t size);
void vcom0_putc(uint8_t c);
void vcom0_putstring(const char *s);

bool vcom0_rxrdy(void);
uint8_t vcom0_getc(void);

bool vcom_process_input(uint8_t ch, uint8_t c);

void VCOM0_rx_IRQHandler(void);
void VCOM0_tx_IRQHandler(void);

#define PIRET_PROMPTRQ 1u
#define PIRET_AUTONUL 0x10u

#endif