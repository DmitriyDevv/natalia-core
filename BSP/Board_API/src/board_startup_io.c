#include "board_startup_io.h"

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "gpio.h"

#ifndef NATALIA_NAND_PS_OFF_LEVEL
#error "NATALIA_NAND_PS_OFF_LEVEL must be defined as 0 or 1"
#endif

#if ((NATALIA_NAND_PS_OFF_LEVEL != 0) && (NATALIA_NAND_PS_OFF_LEVEL != 1))
#error "NATALIA_NAND_PS_OFF_LEVEL must be 0 (LOW) or 1 (HIGH)"
#endif

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#if (NATALIA_NAND_PS_OFF_LEVEL == 1)
#define NAND_POWER_OFF_LEVEL GPIO_LEVEL_HIGH
#else
#define NAND_POWER_OFF_LEVEL GPIO_LEVEL_LOW
#endif

#if defined(NATALIA_ENABLE_UNICAN_DRIVER) && (NATALIA_ENABLE_UNICAN_DRIVER != 0)
#define BOARD_STARTUP_CAN_ENABLED 1
#else
#define BOARD_STARTUP_CAN_ENABLED 0
#endif

#if defined(NATALIA_ENABLE_NAND_DRIVER)
#define BOARD_STARTUP_NAND_ENABLED 1
#else
#define BOARD_STARTUP_NAND_ENABLED 0
#endif

#if defined(NATALIA_ENABLE_PED_REG_DRIVER) && (NATALIA_ENABLE_PED_REG_DRIVER != 0)
#define BOARD_STARTUP_PED_ENABLED 1
#else
#define BOARD_STARTUP_PED_ENABLED 0
#endif

#if defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && (NATALIA_ENABLE_USB_DEVICE_DRIVER != 0)
#define BOARD_STARTUP_USB_ENABLED 1
#else
#define BOARD_STARTUP_USB_ENABLED 0
#endif

#if (BOARD_STARTUP_CAN_ENABLED || BOARD_STARTUP_NAND_ENABLED || BOARD_STARTUP_PED_ENABLED)
static BoardStatus configure_output(BoardPinId pin_id, GpioLevel level) {
    static const GpioConfig output_low_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    static const GpioConfig output_high_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_HIGH
    };

    if (level == GPIO_LEVEL_HIGH) {
        return gpio_configure(pin_id, &output_high_config);
    }

    return gpio_configure(pin_id, &output_low_config);
}
#endif

#if (BOARD_STARTUP_CAN_ENABLED || BOARD_STARTUP_NAND_ENABLED || BOARD_STARTUP_USB_ENABLED)
static BoardStatus configure_input(BoardPinId pin_id) {
    static const GpioConfig input_no_pull_config = {
        .mode = GPIO_MODE_INPUT,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_LOW,
        .initial_level = GPIO_LEVEL_LOW
    };

    return gpio_configure(pin_id, &input_no_pull_config);
}
#endif

static BoardStatus disconnect_pins(const BoardPinId* pins, size_t count) {
    size_t index;
    BoardStatus status;

    if ((pins == NULL) && (count != 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    for (index = 0U; index < count; ++index) {
        status = gpio_set_disconnected(pins[index]);
        if (status != BOARD_OK) {
            return status;
        }
    }

    return BOARD_OK;
}

static BoardStatus configure_ped_control(void) {
#if BOARD_STARTUP_PED_ENABLED
    return configure_output(BOARD_PIN_PU_PED_PS, GPIO_LEVEL_LOW);
#else
    return gpio_set_disconnected(BOARD_PIN_PU_PED_PS);
#endif
}

static BoardStatus configure_nand_control(void) {
#if BOARD_STARTUP_NAND_ENABLED
    BoardStatus status;

    status = configure_output(BOARD_PIN_PU_NAND1_PS, NAND_POWER_OFF_LEVEL);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_output(BOARD_PIN_PU_NAND2_PS, NAND_POWER_OFF_LEVEL);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_input(BOARD_PIN_PU_NAND1_PSON);
    if (status != BOARD_OK) {
        return status;
    }

    return configure_input(BOARD_PIN_PU_NAND2_PSON);
#else
    static const BoardPinId nand_pins[] = {
        BOARD_PIN_PU_NAND1_PS,
        BOARD_PIN_PU_NAND2_PS,
        BOARD_PIN_PU_NAND1_PSON,
        BOARD_PIN_PU_NAND2_PSON
    };

    return disconnect_pins(nand_pins, ARRAY_SIZE(nand_pins));
#endif
}

static BoardStatus configure_can_control(void) {
#if BOARD_STARTUP_CAN_ENABLED
    BoardStatus status;

    status = configure_output(BOARD_PIN_PU_CAN1_SHDN, GPIO_LEVEL_LOW);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_output(BOARD_PIN_PU_CAN1_S, GPIO_LEVEL_LOW);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_output(BOARD_PIN_CAN1_TX, GPIO_LEVEL_HIGH);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_input(BOARD_PIN_CAN1_RX);
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_output(BOARD_PIN_PU_CAN2_SHDN, GPIO_LEVEL_HIGH);
    if (status != BOARD_OK) {
        return status;
    }

    return configure_output(BOARD_PIN_PU_CAN2_S, GPIO_LEVEL_LOW);
#else
    static const BoardPinId can_pins[] = {
        BOARD_PIN_PU_CAN1_SHDN,
        BOARD_PIN_PU_CAN1_S,
        BOARD_PIN_CAN1_TX,
        BOARD_PIN_CAN1_RX,
        BOARD_PIN_PU_CAN2_SHDN,
        BOARD_PIN_PU_CAN2_S
    };

    return disconnect_pins(can_pins, ARRAY_SIZE(can_pins));
#endif
}

static BoardStatus configure_usb_control(void) {
#if BOARD_STARTUP_USB_ENABLED
    return configure_input(BOARD_PIN_PU_USB_VBUS);
#else
    return gpio_set_disconnected(BOARD_PIN_PU_USB_VBUS);
#endif
}

static BoardStatus disconnect_inactive_interfaces(void) {
    static const BoardPinId disconnected_pins[] = {
        BOARD_PIN_QSPI_BK1_NCS,
        BOARD_PIN_QSPI_BK1_IO0,
        BOARD_PIN_QSPI_BK1_IO1,
        BOARD_PIN_QSPI_BK1_IO2,
        BOARD_PIN_QSPI_BK1_IO3,
        BOARD_PIN_QSPI_BK2_NCS,
        BOARD_PIN_QSPI_BK2_IO0,
        BOARD_PIN_QSPI_BK2_IO1,
        BOARD_PIN_QSPI_BK2_IO2,
        BOARD_PIN_QSPI_BK2_IO3,
        BOARD_PIN_QSPI_CLK,

        BOARD_PIN_SPI1_NSS,
        BOARD_PIN_SPI1_SCK,
        BOARD_PIN_SPI1_MISO,
        BOARD_PIN_SPI1_MOSI,
        BOARD_PIN_SPI3_NSS,
        BOARD_PIN_SPI3_SCK,
        BOARD_PIN_SPI3_MISO,
        BOARD_PIN_SPI3_MOSI,

        BOARD_PIN_I2C1_SCL,
        BOARD_PIN_I2C1_SDA,
        BOARD_PIN_I2C2_SCL,
        BOARD_PIN_I2C2_SDA,

        BOARD_PIN_I2C3_SCL,
        BOARD_PIN_I2C3_SDA,

        BOARD_PIN_USB_DM,
        BOARD_PIN_USB_DP,

        BOARD_PIN_CAN2_RX,
        BOARD_PIN_CAN2_TX,

        BOARD_PIN_USART2_RX,
        BOARD_PIN_USART2_TX,

        BOARD_PIN_RTC_OUT,

        BOARD_PIN_PED_REG_DATA0,
        BOARD_PIN_PED_REG_DATA1,
        BOARD_PIN_PED_REG_DATA2,
        BOARD_PIN_PED_REG_DATA3,
        BOARD_PIN_PED_REG_DATA4,
        BOARD_PIN_PED_REG_DATA5,
        BOARD_PIN_PED_REG_DATA6,
        BOARD_PIN_PED_REG_DATA7,
        BOARD_PIN_PED_REG_DATA8,
        BOARD_PIN_PED_REG_DATA9,
        BOARD_PIN_PED_REG_DATA10,
        BOARD_PIN_PED_REG_DATA11,
        BOARD_PIN_PED_REG_DATA12,
        BOARD_PIN_PED_REG_DATA13,
        BOARD_PIN_PED_REG_DATA14,
        BOARD_PIN_PED_REG_DATA15,

        BOARD_PIN_PED_REG_ADDR0,
        BOARD_PIN_PED_REG_ADDR1,
        BOARD_PIN_PED_REG_ADDR2,
        BOARD_PIN_PED_REG_ADDR3,
        BOARD_PIN_PED_REG_ADDR4,
        BOARD_PIN_PED_REG_ADDR5,
        BOARD_PIN_PED_REG_ADDR6,
        BOARD_PIN_PED_REG_ADDR7,

        BOARD_PIN_PED_REG_DIR,
        BOARD_PIN_PED_REG_WRCLK,
        BOARD_PIN_PED_REG_READY,

        BOARD_PIN_PED_TG,
        BOARD_PIN_PED_TGRES,
        BOARD_PIN_PED_INHIBIT,
        BOARD_PIN_PED_SLEEP,
        BOARD_PIN_PED_PSON
    };

    return disconnect_pins(disconnected_pins, ARRAY_SIZE(disconnected_pins));
}

static BoardStatus configure_adc_input(void) {
    return gpio_set_disconnected(BOARD_PIN_ADC12_IN5);
}

BoardStatus board_startup_io_init(void) {
    BoardStatus status;

    status = configure_ped_control();
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_nand_control();
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_can_control();
    if (status != BOARD_OK) {
        return status;
    }

    status = configure_usb_control();
    if (status != BOARD_OK) {
        return status;
    }

    status = disconnect_inactive_interfaces();
    if (status != BOARD_OK) {
        return status;
    }

    return configure_adc_input();
}
