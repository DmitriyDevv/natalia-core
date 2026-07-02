#include "qspi_private.h"

BoardStatus qspi_dma_init(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus qspi_dma_write(const void* buffer, uint32_t size) {
    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_ERR_UNSUPPORTED;
}
