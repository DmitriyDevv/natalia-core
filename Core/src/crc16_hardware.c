#include "crc16.h"

#include "stm32l4xx.h"

#define CRC16_HARDWARE_POLY 0x1021UL
#define CRC16_HARDWARE_INIT 0xFFFFUL

uint16_t crc16_ccitt(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    volatile uint8_t *data_register = (volatile uint8_t *)&CRC->DR;
    size_t i;

    if ((bytes == 0) && (size > 0U)) {
        return (uint16_t)CRC16_HARDWARE_INIT;
    }

    if ((RCC->AHB1ENR & RCC_AHB1ENR_CRCEN) == 0UL) {
        RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
        (void)RCC->AHB1ENR;
    }

    CRC->POL = CRC16_HARDWARE_POLY;
    CRC->INIT = CRC16_HARDWARE_INIT;
    CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_RESET;

    for (i = 0U; i < size; ++i) {
        *data_register = bytes[i];
    }

    return (uint16_t)(CRC->DR & 0xFFFFUL);
}
