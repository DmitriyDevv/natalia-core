#include "usart2.h"

#include <stddef.h>

#include "stm32l496xx.h"

BoardStatus usart2_write_byte(uint8_t byte) {
    while ((USART2->ISR & USART_ISR_TXE) == 0U) {}

    USART2->TDR = (uint32_t)byte;

    return BOARD_OK;
}

BoardStatus usart2_write(const uint8_t *data, size_t size) {
    size_t index;

    if ((data == NULL) && (size != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    for (index = 0U; index < size; ++index) {
        (void)usart2_write_byte(data[index]);
    }

    while ((USART2->ISR & USART_ISR_TC) == 0U) {}

    return BOARD_OK;
}
