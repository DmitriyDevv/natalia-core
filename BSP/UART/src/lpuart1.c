#include "lpuart1.h"

#include <stddef.h>

#include "stm32l496xx.h"
#include "timebase.h"

#define LPUART1_TX_TIMEOUT_MS 10U

static BoardStatus lpuart1_wait_flag(uint32_t flag) {
    uint32_t start_ms = timebase_millis();

    while ((LPUART1->ISR & flag) == 0U) {
        if (timebase_elapsed(start_ms, LPUART1_TX_TIMEOUT_MS)) {
            return BOARD_ERR_TIMEOUT;
        }
    }

    return BOARD_OK;
}

BoardStatus lpuart1_write_byte(uint8_t byte) {
    if (lpuart1_wait_flag(USART_ISR_TXE) != BOARD_OK) {
        return BOARD_ERR_TIMEOUT;
    }

    LPUART1->TDR = (uint32_t)byte;

    return BOARD_OK;
}

BoardStatus lpuart1_write(const uint8_t* data, size_t size) {
    size_t index;

    if ((data == NULL) && (size != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    for (index = 0U; index < size; ++index) {
        if (lpuart1_write_byte(data[index]) != BOARD_OK) {
            return BOARD_ERR_TIMEOUT;
        }
    }

    return lpuart1_wait_flag(USART_ISR_TC);
}

BoardStatus lpuart1_write_string(const char* string) {
    if (string == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    while (*string != '\0') {
        if ((*string == '\n') && (lpuart1_write_byte((uint8_t)'\r') != BOARD_OK)) {
            return BOARD_ERR_TIMEOUT;
        }

        if (lpuart1_write_byte((uint8_t)*string) != BOARD_OK) {
            return BOARD_ERR_TIMEOUT;
        }
        ++string;
    }

    return lpuart1_wait_flag(USART_ISR_TC);
}
