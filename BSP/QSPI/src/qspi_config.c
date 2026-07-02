#include "qspi_private.h"

#include "board_pins.h"
#include "gpio.h"

static const GpioConfig qspi_data_pin_config = {
    .mode = GPIO_MODE_ALTERNATE,
    .pull = GPIO_PULL_UP,
    .output_type = GPIO_OUTPUT_PUSH_PULL,
    .speed = GPIO_SPEED_HIGH,
    .initial_level = GPIO_LEVEL_LOW
};

static const GpioConfig qspi_ncs_pin_config = {
    .mode = GPIO_MODE_ALTERNATE,
    .pull = GPIO_PULL_UP,
    .output_type = GPIO_OUTPUT_PUSH_PULL,
    .speed = GPIO_SPEED_VERY_HIGH,
    .initial_level = GPIO_LEVEL_HIGH
};

static BoardStatus configure_data_pin(BoardPinId pin_id) {
    return gpio_configure(pin_id, &qspi_data_pin_config);
}

static BoardStatus configure_ncs_pin(BoardPinId pin_id) {
    return gpio_configure(pin_id, &qspi_ncs_pin_config);
}

static BoardStatus configure_bank1_pins(void) {
    BoardStatus status;

    status = configure_ncs_pin(BOARD_PIN_QSPI_BK1_NCS);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK1_IO0);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK1_IO1);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK1_IO2);
    if (status != BOARD_OK) {
        return status;
    }

    return configure_data_pin(BOARD_PIN_QSPI_BK1_IO3);
}

static BoardStatus configure_bank2_pins(void) {
    BoardStatus status;

    status = configure_ncs_pin(BOARD_PIN_QSPI_BK2_NCS);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK2_IO0);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK2_IO1);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_data_pin(BOARD_PIN_QSPI_BK2_IO2);
    if (status != BOARD_OK) {
        return status;
    }

    return configure_data_pin(BOARD_PIN_QSPI_BK2_IO3);
}

BoardStatus qspi_configure_pins(QspiBank bank) {
    BoardStatus status;

    status = configure_data_pin(BOARD_PIN_QSPI_CLK);
    if (status != BOARD_OK) {
        return status;
    }

    if (bank == QSPI_BANK_1) {
        return configure_bank1_pins();
    }

    if (bank == QSPI_BANK_2) {
        return configure_bank2_pins();
    }

    return BOARD_ERR_INVALID_ARG;
}