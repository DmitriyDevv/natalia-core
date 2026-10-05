#include "crc16.h"

#include <string.h>

#include "stm32l4xx.h"

#define CRC16_HARDWARE_POLY 0x1021UL
#define CRC16_HARDWARE_INIT 0xFFFFUL

static uint8_t crc16_hardware_configured;

static void crc16_hardware_configure(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;

    CRC->POL = CRC16_HARDWARE_POLY;
    CRC->INIT = CRC16_HARDWARE_INIT;

    crc16_hardware_configured = 1U;
}

uint16_t crc16_ccitt(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    volatile uint8_t *data_register_byte = (volatile uint8_t *)&CRC->DR;
    uint32_t word;

    if ((bytes == 0) && (size > 0U)) {
        return (uint16_t)CRC16_HARDWARE_INIT;
    }

    if (crc16_hardware_configured == 0U) {
        crc16_hardware_configure();
    }

    CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_RESET;

    while ((size > 0U) && ((((uintptr_t)bytes) & 3U) != 0U)) {
        *data_register_byte = *bytes;
        ++bytes;
        --size;
    }

    while (size >= 4U) {
        (void)memcpy(&word, bytes, sizeof(word));
        CRC->DR = __REV(word);
        bytes += 4U;
        size -= 4U;
    }

    while (size > 0U) {
        *data_register_byte = *bytes;
        ++bytes;
        --size;
    }

    return (uint16_t)(CRC->DR & 0xFFFFUL);
}
