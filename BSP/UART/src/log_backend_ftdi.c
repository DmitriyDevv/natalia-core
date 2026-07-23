#include "log_backend.h"

#include "ftdi.h"

BoardStatus log_backend_init(void) {
    BoardStatus status;

    status = ftdi_init();
    if (status != BOARD_OK) {
        return status;
    }

    ftdi_set_mode(FTDI_MODE_LOG);

    return BOARD_OK;
}

void log_backend_write(const uint8_t *data, size_t size) {
    size_t accepted;

    (void)ftdi_write(data, size, &accepted);
}
