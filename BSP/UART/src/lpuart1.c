#include "lpuart1.h"

#include <stddef.h>

#include "stm32l496xx.h"

BoardStatus lpuart1_write_byte(uint8_t byte) {
    while ((LPUART1->ISR & USART_ISR_TXE) == 0U) {}

    LPUART1->TDR = (uint32_t)byte;

    return BOARD_OK;
}

BoardStatus lpuart1_write(const uint8_t* data, size_t size) {
    size_t index;

    if ((data == NULL) && (size != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    for (index = 0U; index < size; ++index) {
        (void)lpuart1_write_byte(data[index]);
    }

    while ((LPUART1->ISR & USART_ISR_TC) == 0U) {}

    return BOARD_OK;
}

BoardStatus lpuart1_write_string(const char* string) {
    if (string == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    while (*string != '\0') {
        if (*string == '\n') {
            (void)lpuart1_write_byte((uint8_t)'\r');
        }

        (void)lpuart1_write_byte((uint8_t)*string);
        ++string;
    }

    while ((LPUART1->ISR & USART_ISR_TC) == 0U) {}

    return BOARD_OK;
}
