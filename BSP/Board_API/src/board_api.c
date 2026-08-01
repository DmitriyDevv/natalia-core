#include "board_api.h"

#include <string.h>

#include "crc16.h"

#include "board_startup_io.h"
#include "gpio.h"
#include "rtc.h"

#if defined(NATALIA_ENABLE_NAND_DRIVER)
#include "nand_storage.h"
#include "qspi.h"
#endif
#if defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && (NATALIA_ENABLE_USB_DEVICE_DRIVER != 0) && \
(!defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) || (NATALIA_ENABLE_BOARD_TEST_HOOKS == 0))
#include "usb_cdc.h"
#endif

#if defined(NATALIA_ENABLE_PED_REG_DRIVER) && (NATALIA_ENABLE_PED_REG_DRIVER != 0)
#include "ped_reg.h"
#endif

#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
#include "ina219.h"
#include "ina219_config.h"
#endif

#if defined(NATALIA_ENABLE_MRAM_DRIVER) && (NATALIA_ENABLE_MRAM_DRIVER != 0)
#include "mram.h"
#endif

#if defined(NATALIA_ENABLE_FTDI_DRIVER) && (NATALIA_ENABLE_FTDI_DRIVER != 0)
#include "ftdi.h"
#endif

#if defined(NATALIA_ENABLE_UNICAN_DRIVER) && (NATALIA_ENABLE_UNICAN_DRIVER != 0)
#include "can1.h"
#include "unican.h"
#endif

#ifndef NATALIA_NAND_PS_OFF_LEVEL
#define NATALIA_NAND_PS_OFF_LEVEL 1
#endif

#ifndef NATALIA_NAND_POWER_CONTROL
#define NATALIA_NAND_POWER_CONTROL 1
#endif

#if (NATALIA_NAND_PS_OFF_LEVEL == 1)
#define BOARD_NAND_POWER_OFF_LEVEL GPIO_LEVEL_HIGH
#define BOARD_NAND_POWER_ON_LEVEL GPIO_LEVEL_LOW
#else
#define BOARD_NAND_POWER_OFF_LEVEL GPIO_LEVEL_LOW
#define BOARD_NAND_POWER_ON_LEVEL GPIO_LEVEL_HIGH
#endif

#define BOARD_NAND_POWER_TIMEOUT 1000000UL

#if defined(NATALIA_ENABLE_MRAM_DRIVER) && (NATALIA_ENABLE_MRAM_DRIVER != 0)

#define BOARD_MRAM_REGION_SIZE 1024U
#define BOARD_MRAM_CRC_OFFSET (BOARD_MRAM_REGION_SIZE - 2U)
#define BOARD_MRAM_DATA_SIZE BOARD_MRAM_CRC_OFFSET

#define BOARD_MRAM_TEST_RESULT1_OFFSET 0x2000U
#define BOARD_MRAM_TEST_RESULT2_OFFSET 0x4000U

static uint8_t board_mram_region_buffer[BOARD_MRAM_DATA_SIZE];

#else

#define BOARD_MRAM_COPY_COUNT 2U
#define BOARD_MRAM_COPY_SIZE 1024U

static uint8_t board_mram_stub_storage[BOARD_MRAM_COPY_COUNT][BOARD_MRAM_COPY_SIZE];
static uint8_t board_mram_stub_test_result[BOARD_MRAM_COPY_COUNT][2][BOARD_MRAM_TEST_RESULT_SIZE];
static uint8_t board_mram_stub_initialized;

#endif

#if defined(NATALIA_ENABLE_NAND_DRIVER)
static uint8_t board_nand_storage_initialized;
static uint8_t board_nand_active_bank_id;
#endif

#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
static Ina219Device board_ina219_pu_device;
static Ina219Device board_ina219_ped_device;
static uint8_t board_ina219_pu_initialized;
static uint8_t board_ina219_ped_initialized;
#endif

BoardStatus board_init_hardware(void) {
    BoardStatus status;

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    board_nand_storage_initialized = 0U;
    board_nand_active_bank_id = 0U;
#endif

#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
    board_ina219_pu_initialized = 0U;
    board_ina219_ped_initialized = 0U;
#endif

    status = board_startup_io_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_init();
    if (status != BOARD_OK) {
        return status;
    }

#if defined(NATALIA_ENABLE_MRAM_DRIVER) && (NATALIA_ENABLE_MRAM_DRIVER != 0)
    status = mram_init(MRAM_BANK_1);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_init(MRAM_BANK_2);
    if (status != BOARD_OK) {
        return status;
    }
#endif

#if defined(NATALIA_ENABLE_PED_REG_DRIVER) && (NATALIA_ENABLE_PED_REG_DRIVER != 0)
    status = ped_reg_init();
    if (status != BOARD_OK) {
        return status;
    }
#endif

#if defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && (NATALIA_ENABLE_USB_DEVICE_DRIVER != 0) && \
(!defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) || (NATALIA_ENABLE_BOARD_TEST_HOOKS == 0))
    status = usb_cdc_init();
    if (status != BOARD_OK) {
        return status;
    }
#endif

#if defined(NATALIA_ENABLE_FTDI_DRIVER) && (NATALIA_ENABLE_FTDI_DRIVER != 0)
    status = ftdi_init();
    if (status != BOARD_OK) {
        return status;
    }
#endif

    return BOARD_OK;
}

BoardStatus board_enter_safe_config(void) {
    BoardStatus status;

#if defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && \
(NATALIA_ENABLE_USB_DEVICE_DRIVER != 0) && \
(!defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) || \
(NATALIA_ENABLE_BOARD_TEST_HOOKS == 0))
    (void)usb_cdc_deinit();
#endif

    status = board_startup_io_init();

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    board_nand_storage_initialized = 0U;
    board_nand_active_bank_id = 0U;
#endif

#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
    board_ina219_pu_initialized = 0U;
    board_ina219_ped_initialized = 0U;
#endif

    return status;
}

#if defined(NATALIA_ENABLE_MRAM_DRIVER) && (NATALIA_ENABLE_MRAM_DRIVER != 0)

static BoardStatus board_mram_copy_to_bank(uint8_t copy_id, MramBank* bank) {
    switch (copy_id) {
    case 1U:
        *bank = MRAM_BANK_1;
        return BOARD_OK;
    case 2U:
        *bank = MRAM_BANK_2;
        return BOARD_OK;
    default:
        return BOARD_ERR_INVALID_ARG;
    }
}

static BoardStatus board_mram_check_range(uint32_t offset, size_t size) {
    if (offset > BOARD_MRAM_DATA_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > (size_t)(BOARD_MRAM_DATA_SIZE - offset)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

static BoardStatus board_mram_update_crc(MramBank bank) {
    BoardStatus status;
    uint16_t crc;
    uint8_t crc_bytes[2];

    status = mram_read(bank, 0U, board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    if (status != BOARD_OK) {
        return status;
    }

    crc = crc16_ccitt(board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    crc_bytes[0] = (uint8_t)(crc & 0xFFU);
    crc_bytes[1] = (uint8_t)((crc >> 8) & 0xFFU);

    return mram_write(bank, BOARD_MRAM_CRC_OFFSET, crc_bytes, sizeof(crc_bytes));
}

BoardStatus board_mram_read(uint8_t copy_id,
                            uint32_t offset,
                            void* buffer,
                            size_t size) {
    MramBank bank;
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_mram_copy_to_bank(copy_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_check_range(offset, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    return mram_read(bank, offset, buffer, size);
}

BoardStatus board_mram_write(uint8_t copy_id,
                             uint32_t offset,
                             const void* buffer,
                             size_t size) {
    MramBank bank;
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_mram_copy_to_bank(copy_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_check_range(offset, size);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        status = mram_write(bank, offset, buffer, size);
        if (status != BOARD_OK) {
            return status;
        }
    }

    return board_mram_update_crc(bank);
}

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t* is_valid) {
    MramBank bank;
    BoardStatus status;
    uint16_t computed;
    uint16_t stored;
    uint8_t crc_bytes[2];

    if (is_valid == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_valid = 0U;

    status = board_mram_copy_to_bank(copy_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(bank, 0U, board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(bank, BOARD_MRAM_CRC_OFFSET, crc_bytes, sizeof(crc_bytes));
    if (status != BOARD_OK) {
        return status;
    }

    computed = crc16_ccitt(board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    stored = (uint16_t)((uint16_t)crc_bytes[0] | (uint16_t)((uint16_t)crc_bytes[1] << 8));

    if (computed == stored) {
        *is_valid = 1U;
    }

    return BOARD_OK;
}

BoardStatus board_mram_restore_copy(uint8_t source_copy_id,
                                    uint8_t target_copy_id) {
    MramBank source_bank;
    MramBank target_bank;
    BoardStatus status;
    uint8_t crc_bytes[2];

    status = board_mram_copy_to_bank(source_copy_id, &source_bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_copy_to_bank(target_copy_id, &target_bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(source_bank, 0U, board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(source_bank, BOARD_MRAM_CRC_OFFSET, crc_bytes, sizeof(crc_bytes));
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_write(target_bank, 0U, board_mram_region_buffer, BOARD_MRAM_DATA_SIZE);
    if (status != BOARD_OK) {
        return status;
    }

    return mram_write(target_bank, BOARD_MRAM_CRC_OFFSET, crc_bytes, sizeof(crc_bytes));
}

static BoardStatus board_mram_test_result_offset(uint8_t nand_bank, uint32_t* offset) {
    if (nand_bank == 1U) {
        *offset = BOARD_MRAM_TEST_RESULT1_OFFSET;
        return BOARD_OK;
    }
    if (nand_bank == 2U) {
        *offset = BOARD_MRAM_TEST_RESULT2_OFFSET;
        return BOARD_OK;
    }
    return BOARD_ERR_INVALID_ARG;
}

BoardStatus board_mram_write_test_result(uint8_t copy_id,
                                         uint8_t nand_bank,
                                         const void* data,
                                         size_t size) {
    MramBank bank;
    uint32_t offset;
    uint16_t crc;
    uint8_t crc_bytes[BOARD_MRAM_TEST_RESULT_CRC_SIZE];
    BoardStatus status;

    if ((data == 0) || (size != BOARD_MRAM_TEST_RESULT_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_mram_copy_to_bank(copy_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_test_result_offset(nand_bank, &offset);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_write(bank, offset, data, size);
    if (status != BOARD_OK) {
        return status;
    }

    crc = crc16_ccitt(data, size);
    crc_bytes[0] = (uint8_t)(crc & 0xFFU);
    crc_bytes[1] = (uint8_t)((crc >> 8) & 0xFFU);

    return mram_write(bank, offset + size, crc_bytes, sizeof(crc_bytes));
}

BoardStatus board_mram_read_test_result(uint8_t copy_id,
                                        uint8_t nand_bank,
                                        void* data,
                                        size_t size,
                                        uint8_t* is_valid,
                                        uint8_t* crc_out) {
    MramBank bank;
    uint32_t offset;
    uint16_t crc;
    uint16_t stored_crc;
    uint8_t crc_bytes[BOARD_MRAM_TEST_RESULT_CRC_SIZE];
    BoardStatus status;

    if ((data == 0) || (size != BOARD_MRAM_TEST_RESULT_SIZE) || (is_valid == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_valid = 0U;

    status = board_mram_copy_to_bank(copy_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_test_result_offset(nand_bank, &offset);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(bank, offset, data, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read(bank, offset + size, crc_bytes, sizeof(crc_bytes));
    if (status != BOARD_OK) {
        return status;
    }

    crc = crc16_ccitt(data, size);
    stored_crc = (uint16_t)((uint16_t)crc_bytes[0] | ((uint16_t)crc_bytes[1] << 8));
    *is_valid = (crc == stored_crc) ? 1U : 0U;

    if (crc_out != 0) {
        crc_out[0] = crc_bytes[0];
        crc_out[1] = crc_bytes[1];
    }

    return BOARD_OK;
}

#else

static BoardStatus board_mram_stub_check_range(uint8_t copy_id,
                                               uint32_t offset,
                                               size_t size,
                                               uint8_t** storage) {
    uint32_t copy_index;

    if ((copy_id == 0U) || (copy_id > BOARD_MRAM_COPY_COUNT)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((offset > BOARD_MRAM_COPY_SIZE) ||
        (size > ((size_t)BOARD_MRAM_COPY_SIZE - (size_t)offset))) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (storage != 0) {
        copy_index = (uint32_t)copy_id - 1UL;
        *storage = &board_mram_stub_storage[copy_index][offset];
    }

    return BOARD_OK;
}

static void board_mram_stub_init_once(void) {
    if (board_mram_stub_initialized == 0U) {
        (void)memset(board_mram_stub_storage, 0, sizeof(board_mram_stub_storage));
        board_mram_stub_initialized = 1U;
    }
}

BoardStatus board_mram_read(uint8_t copy_id,
                            uint32_t offset,
                            void* buffer,
                            size_t size) {
    uint8_t* storage = 0;
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_mram_stub_init_once();

    status = board_mram_stub_check_range(copy_id, offset, size, &storage);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        (void)memcpy(buffer, storage, size);
    }

    return BOARD_OK;
}

BoardStatus board_mram_write(uint8_t copy_id,
                             uint32_t offset,
                             const void* buffer,
                             size_t size) {
    uint8_t* storage = 0;
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_mram_stub_init_once();

    status = board_mram_stub_check_range(copy_id, offset, size, &storage);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        (void)memcpy(storage, buffer, size);
    }

    return BOARD_OK;
}

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t* is_valid) {
    BoardStatus status;

    if (is_valid == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_mram_stub_init_once();

    status = board_mram_stub_check_range(copy_id, 0U, 0U, 0);
    if (status != BOARD_OK) {
        return status;
    }

    *is_valid = 1U;

    return BOARD_OK;
}

BoardStatus board_mram_restore_copy(uint8_t source_copy_id,
                                    uint8_t target_copy_id) {
    uint8_t* source = 0;
    uint8_t* target = 0;
    BoardStatus status;

    board_mram_stub_init_once();

    status = board_mram_stub_check_range(source_copy_id,
                                         0U,
                                         BOARD_MRAM_COPY_SIZE,
                                         &source);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_stub_check_range(target_copy_id,
                                         0U,
                                         BOARD_MRAM_COPY_SIZE,
                                         &target);
    if (status != BOARD_OK) {
        return status;
    }

    (void)memcpy(target, source, BOARD_MRAM_COPY_SIZE);

    return BOARD_OK;
}

BoardStatus board_mram_write_test_result(uint8_t copy_id,
                                         uint8_t nand_bank,
                                         const void* data,
                                         size_t size) {
    if ((data == 0) || (size != BOARD_MRAM_TEST_RESULT_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((copy_id == 0U) || (copy_id > BOARD_MRAM_COPY_COUNT)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((nand_bank != 1U) && (nand_bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_mram_stub_init_once();

    (void)memcpy(board_mram_stub_test_result[copy_id - 1U][nand_bank - 1U],
                 data, size);

    return BOARD_OK;
}

BoardStatus board_mram_read_test_result(uint8_t copy_id,
                                        uint8_t nand_bank,
                                        void* data,
                                        size_t size,
                                        uint8_t* is_valid,
                                        uint8_t* crc_out) {
    if ((data == 0) || (size != BOARD_MRAM_TEST_RESULT_SIZE) || (is_valid == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((copy_id == 0U) || (copy_id > BOARD_MRAM_COPY_COUNT)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((nand_bank != 1U) && (nand_bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_mram_stub_init_once();

    (void)memcpy(data,
                 board_mram_stub_test_result[copy_id - 1U][nand_bank - 1U],
                 size);
    *is_valid = 1U;

    if (crc_out != 0) {
        uint16_t crc = crc16_ccitt(data, size);
        crc_out[0] = (uint8_t)(crc & 0xFFU);
        crc_out[1] = (uint8_t)((crc >> 8) & 0xFFU);
    }

    return BOARD_OK;
}

#endif

static BoardStatus board_nand_get_pins(uint8_t bank_id,
                                       BoardPinId* power_pin,
                                       BoardPinId* power_status_pin) {
    if ((power_pin == 0) || (power_status_pin == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bank_id == 1U) {
        *power_pin = BOARD_PIN_PU_NAND1_PS;
        *power_status_pin = BOARD_PIN_PU_NAND1_PSON;
        return BOARD_OK;
    }

    if (bank_id == 2U) {
        *power_pin = BOARD_PIN_PU_NAND2_PS;
        *power_status_pin = BOARD_PIN_PU_NAND2_PSON;
        return BOARD_OK;
    }

    return BOARD_ERR_INVALID_ARG;
}

#if defined(NATALIA_ENABLE_NAND_DRIVER)
static BoardStatus board_nand_get_storage_bank(uint8_t bank_id,
                                               NandMt29fBank* bank) {
    if (bank == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bank_id == 1U) {
        *bank = NAND_MT29F_BANK_1;
        return BOARD_OK;
    }

    if (bank_id == 2U) {
        *bank = NAND_MT29F_BANK_2;
        return BOARD_OK;
    }

    return BOARD_ERR_INVALID_ARG;
}

static BoardStatus board_nand_storage_init_once(void) {
    BoardStatus status;

    if (board_nand_storage_initialized != 0U) {
        return BOARD_OK;
    }

    status = nand_storage_init();
    if (status != BOARD_OK) {
        return status;
    }

    board_nand_storage_initialized = 1U;

    return BOARD_OK;
}

static BoardStatus board_nand_require_active(uint8_t bank_id) {
    if ((bank_id != 1U) && (bank_id != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_nand_active_bank_id != bank_id) {
        return BOARD_ERR_NOT_READY;
    }

    return BOARD_OK;
}
#endif

static BoardStatus board_nand_wait_power_state(uint8_t bank_id,
                                               uint8_t expected_powered) {
    BoardPinId power_pin;
    BoardPinId power_status_pin;
    GpioLevel level;
    uint32_t timeout = BOARD_NAND_POWER_TIMEOUT;
    BoardStatus status;

    status = board_nand_get_pins(bank_id, &power_pin, &power_status_pin);
    if (status != BOARD_OK) {
        return status;
    }

    (void)power_pin;

    while (timeout > 0U) {
        status = gpio_read(power_status_pin, &level);
        if (status != BOARD_OK) {
            return status;
        }

        if (expected_powered != 0U) {
            if (level == GPIO_LEVEL_HIGH) {
                return BOARD_OK;
            }
        } else {
            if (level == GPIO_LEVEL_LOW) {
                return BOARD_OK;
            }
        }

        --timeout;
    }

    return BOARD_ERR_TIMEOUT;
}

static BoardStatus board_nand_disconnect_pins(uint8_t bank_id) {
    BoardStatus status;

    status = gpio_set_disconnected(BOARD_PIN_QSPI_BK2_CLK);
    if (status != BOARD_OK) {
        return status;
    }

    if (bank_id == 1U) {
        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK1_NCS);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK1_IO0);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK1_IO1);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK1_IO2);
        if (status != BOARD_OK) {
            return status;
        }

        return gpio_set_disconnected(BOARD_PIN_QSPI_BK1_IO3);
    }

    if (bank_id == 2U) {
        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK2_NCS);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK2_IO0);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK2_IO1);
        if (status != BOARD_OK) {
            return status;
        }

        status = gpio_set_disconnected(BOARD_PIN_QSPI_BK2_IO2);
        if (status != BOARD_OK) {
            return status;
        }

        return gpio_set_disconnected(BOARD_PIN_QSPI_BK2_IO3);
    }

    return BOARD_ERR_INVALID_ARG;
}

BoardStatus board_disconnect_signal_lines(BoardSignalTarget target) {
    if (target == BOARD_SIGNAL_NAND1) {
        return board_nand_disconnect_pins(1U);
    }

    if (target == BOARD_SIGNAL_NAND2) {
        return board_nand_disconnect_pins(2U);
    }

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_nand_power_on(uint8_t bank_id) {
#if !defined(NATALIA_ENABLE_NAND_DRIVER)
    (void)bank_id;

    return BOARD_OK;
#elif (NATALIA_NAND_POWER_CONTROL == 0)
    if ((bank_id != 1U) && (bank_id != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
#else
    BoardPinId power_pin;
    BoardPinId power_status_pin;
    BoardStatus status;

    status = board_nand_get_pins(bank_id, &power_pin, &power_status_pin);
    if (status != BOARD_OK) {
        return status;
    }

    (void)power_status_pin;

    status = gpio_write(power_pin, BOARD_NAND_POWER_ON_LEVEL);
    if (status != BOARD_OK) {
        return status;
    }

    return board_nand_wait_power_state(bank_id, 1U);
#endif
}

BoardStatus board_nand_power_off(uint8_t bank_id) {
#if !defined(NATALIA_ENABLE_NAND_DRIVER)
    (void)bank_id;

    return BOARD_OK;
#elif (NATALIA_NAND_POWER_CONTROL == 0)
    if ((bank_id != 1U) && (bank_id != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    (void)board_nand_disconnect(bank_id);

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    board_nand_storage_initialized = 0U;
    board_nand_active_bank_id = 0U;
#endif

    return BOARD_OK;
#else
    BoardPinId power_pin;
    BoardPinId power_status_pin;
    BoardStatus status;

    status = board_nand_get_pins(bank_id, &power_pin, &power_status_pin);
    if (status != BOARD_OK) {
        return status;
    }

    (void)power_status_pin;

    (void)board_nand_disconnect(bank_id);

    status = gpio_write(power_pin, BOARD_NAND_POWER_OFF_LEVEL);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_wait_power_state(bank_id, 0U);

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    if (status == BOARD_OK) {
        board_nand_storage_initialized = 0U;
        board_nand_active_bank_id = 0U;
    }
#endif

    return status;
#endif
}

BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t* is_powered) {
#if !defined(NATALIA_ENABLE_NAND_DRIVER)
    (void)bank_id;

    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = 1U;

    return BOARD_OK;
#elif (NATALIA_NAND_POWER_CONTROL == 0)
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((bank_id != 1U) && (bank_id != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = 1U;

    return BOARD_OK;
#else
    BoardPinId power_pin;
    BoardPinId power_status_pin;
    GpioLevel level;
    BoardStatus status;

    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_get_pins(bank_id, &power_pin, &power_status_pin);
    if (status != BOARD_OK) {
        return status;
    }

    (void)power_pin;

    status = gpio_read(power_status_pin, &level);
    if (status != BOARD_OK) {
        return status;
    }

    *is_powered = (level == GPIO_LEVEL_HIGH) ? 1U : 0U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_connect(uint8_t bank_id) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    NandMt29fBank bank;
    uint8_t is_powered = 0U;
    BoardStatus status;

    status = board_nand_is_powered(bank_id, &is_powered);
    if (status != BOARD_OK) {
        return status;
    }

    if (is_powered == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = board_nand_get_storage_bank(bank_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = qspi_dma_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_storage_init_once();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_storage_mount(bank);
    if (status != BOARD_OK) {
        return status;
    }

    board_nand_active_bank_id = bank_id;

    return BOARD_OK;
#else
    (void)bank_id;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_disconnect(uint8_t bank_id) {
    BoardStatus status;

    status = board_nand_disconnect_pins(bank_id);

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    if ((status == BOARD_OK) && (board_nand_active_bank_id == bank_id)) {
        board_nand_active_bank_id = 0U;
    }
#endif

    return status;
}

BoardStatus board_nand_read(uint8_t bank_id,
                            uint32_t address,
                            void* buffer,
                            size_t size) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    if (size == 0U) {
        return BOARD_OK;
    }

    if ((buffer == 0) || (size != NAND_STORAGE_PACKET_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((address % NAND_STORAGE_PACKET_SIZE) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    return board_nand_read_packet(bank_id,
                                  address / NAND_STORAGE_PACKET_SIZE,
                                  buffer);
#else
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_write(uint8_t bank_id,
                             uint32_t address,
                             const void* buffer,
                             size_t size) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    uint32_t packet_count;
    uint32_t packet_index;
    BoardStatus status;

    if (size == 0U) {
        return BOARD_OK;
    }

    if ((buffer == 0) || (size != NAND_STORAGE_PACKET_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((address % NAND_STORAGE_PACKET_SIZE) != 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    packet_index = address / NAND_STORAGE_PACKET_SIZE;

    status = board_nand_get_committed_packet_count(bank_id, &packet_count);
    if (status != BOARD_OK) {
        return status;
    }

    if (packet_index != packet_count) {
        return BOARD_ERR_INVALID_ARG;
    }

    return board_nand_write_packet(bank_id, buffer);
#else
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_open_write(uint8_t bank_id, uint32_t start_packet_count) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    NandMt29fBank bank;
    BoardStatus status;

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_get_storage_bank(bank_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_open_write(bank, start_packet_count);
#else
    (void)bank_id;
    (void)start_packet_count;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_write_packet(uint8_t bank_id, const void* packet) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (packet == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_write_packet(packet);
#else
    (void)bank_id;
    (void)packet;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_write_poll(uint8_t bank_id, uint8_t* is_idle) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_write_poll(is_idle);
#else
    (void)bank_id;

    if (is_idle == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_idle = 1U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_write_flush(uint8_t bank_id, uint8_t* is_done) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_write_flush(is_done);
#else
    (void)bank_id;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_done = 1U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_open_read(uint8_t bank_id, uint32_t packet_count) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    NandMt29fBank bank;
    BoardStatus status;

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_get_storage_bank(bank_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_open_read(bank, packet_count);
#else
    (void)bank_id;
    (void)packet_count;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_read_packet(uint8_t bank_id,
                                   uint32_t packet_index,
                                   void* packet) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (packet == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_read_packet(packet_index, packet);
#else
    (void)bank_id;
    (void)packet_index;
    (void)packet;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_read_next_packet(uint8_t bank_id,
                                        void* packet,
                                        uint8_t* has_packet) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if ((packet == 0) || (has_packet == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_read_next_packet(packet, has_packet);
#else
    (void)bank_id;
    (void)packet;

    if (has_packet == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *has_packet = 0U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_get_capacity_packets(uint8_t bank_id,
                                            uint32_t* packet_capacity) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (packet_capacity == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_get_capacity_packets(packet_capacity);
#else
    (void)bank_id;

    if (packet_capacity == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *packet_capacity = 65535U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_get_committed_packet_count(uint8_t bank_id,
                                                  uint32_t* packet_count) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (packet_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_get_committed_packet_count(packet_count);
#else
    (void)bank_id;

    if (packet_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *packet_count = 0U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_erase_start(uint8_t bank_id) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    NandMt29fBank bank;
    BoardStatus status;

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_get_storage_bank(bank_id, &bank);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_erase_bank_start(bank);
#else
    (void)bank_id;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t* is_done) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_erase_bank_poll(is_done);
#else
    (void)bank_id;

    if (is_done == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_done = 1U;

    return BOARD_OK;
#endif
}

BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t* is_full) {
#if defined(NATALIA_ENABLE_NAND_DRIVER)
    BoardStatus status;

    if (is_full == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_nand_require_active(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return nand_storage_is_full(is_full);
#else
    (void)bank_id;

    if (is_full == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_full = 0U;

    return BOARD_OK;
#endif
}

#if defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) && (NATALIA_ENABLE_BOARD_TEST_HOOKS != 0)

BoardStatus board_ped_power_on(void) {
    return BOARD_OK;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_OK;
}

BoardStatus board_ped_is_powered(uint8_t* is_powered) {
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = 1U;

    return BOARD_OK;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_OK;
}

BoardStatus board_ped_read_status(uint32_t* status) {
    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *status = 0U;

    return BOARD_OK;
}

BoardStatus board_ped_write_config(const void* config, size_t size) {
    if ((config == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

BoardStatus board_ped_read_event(void* event_buffer,
                                 size_t buffer_size,
                                 size_t* bytes_read) {
    uint8_t* buffer;
    size_t index;
    size_t count;

    if ((event_buffer == 0) && (buffer_size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_read == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    buffer = event_buffer;
    count = buffer_size;

    if (count > 16U) {
        count = 16U;
    }

    for (index = 0U; index < count; ++index) {
        buffer[index] = (uint8_t)(0x30U + index);
    }

    *bytes_read = count;

    return BOARD_OK;
}

BoardStatus board_ped_set_inhibit(uint8_t enabled) {
    (void)enabled;
    return BOARD_OK;
}

BoardStatus board_ped_set_sleep(uint8_t enabled) {
    (void)enabled;
    return BOARD_OK;
}

BoardStatus board_ped_reset_trigger(void) {
    return BOARD_OK;
}

BoardStatus board_ped_take_trigger_events(uint32_t* event_count) {
    if (event_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = 0U;

    return BOARD_OK;
}

#elif defined(NATALIA_ENABLE_PED_REG_DRIVER) && (NATALIA_ENABLE_PED_REG_DRIVER != 0)

BoardStatus board_ped_power_on(void) {
    return ped_reg_power_on();
}

BoardStatus board_ped_power_off(void) {
    return ped_reg_power_off();
}

BoardStatus board_ped_is_powered(uint8_t* is_powered) {
    return ped_reg_is_powered(is_powered);
}

BoardStatus board_ped_reg_init(void) {
    return ped_reg_init();
}

BoardStatus board_ped_read_status(uint32_t* status) {
    return ped_reg_read_status(status);
}

BoardStatus board_ped_write_config(const void* config, size_t size) {
    return ped_reg_write_config(config, size);
}

BoardStatus board_ped_read_event(void* event_buffer,
                                 size_t buffer_size,
                                 size_t* bytes_read) {
    return ped_reg_read_event(event_buffer, buffer_size, bytes_read);
}

BoardStatus board_ped_set_inhibit(uint8_t enabled) {
    return ped_reg_set_inhibit(enabled);
}

BoardStatus board_ped_set_sleep(uint8_t enabled) {
    return ped_reg_set_sleep(enabled);
}

BoardStatus board_ped_reset_trigger(void) {
    return ped_reg_reset_trigger();
}

BoardStatus board_ped_take_trigger_events(uint32_t* event_count) {
    uint8_t pending = 0U;
    BoardStatus status;

    if (event_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = 0U;

    status = ped_reg_take_trigger_pending(&pending);
    if (status != BOARD_OK) {
        return status;
    }

    *event_count = (pending != 0U) ? 1U : 0U;

    return BOARD_OK;
}

#else

BoardStatus board_ped_power_on(void) {
    return BOARD_OK;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_OK;
}

BoardStatus board_ped_is_powered(uint8_t* is_powered) {
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = 1U;

    return BOARD_OK;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_OK;
}

BoardStatus board_ped_read_status(uint32_t* status) {
    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *status = 0U;

    return BOARD_OK;
}

BoardStatus board_ped_write_config(const void* config, size_t size) {
    (void)config;
    (void)size;

    return BOARD_OK;
}

BoardStatus board_ped_read_event(void* event_buffer,
                                 size_t buffer_size,
                                 size_t* bytes_read) {
    (void)event_buffer;
    (void)buffer_size;

    if (bytes_read == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_read = 0U;

    return BOARD_OK;
}

BoardStatus board_ped_set_inhibit(uint8_t enabled) {
    (void)enabled;

    return BOARD_OK;
}

BoardStatus board_ped_set_sleep(uint8_t enabled) {
    (void)enabled;

    return BOARD_OK;
}

BoardStatus board_ped_reset_trigger(void) {
    return BOARD_OK;
}

BoardStatus board_ped_take_trigger_events(uint32_t* event_count) {
    if (event_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = 0U;

    return BOARD_OK;
}

#endif

BoardStatus board_rtc_get_time(InstrumentTime* time) {
    return rtc_get_time(time);
}

BoardStatus board_rtc_set_time(const InstrumentTime* time) {
    return rtc_set_time(time);
}

BoardStatus board_rtc_take_1hz_events(uint32_t* event_count) {
    if (event_count == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = rtc_take_1hz_events();

    return BOARD_OK;
}

#if defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) && (NATALIA_ENABLE_BOARD_TEST_HOOKS != 0)

#define BOARD_USB_TEST_PACKET_SIZE 2048UL

static uint8_t board_usb_test_capture_enabled;
static uint32_t board_usb_test_expected_packet_count;
static uint32_t board_usb_test_acquisition_period_ticks;
static uint32_t board_usb_test_bytes_written;
static uint32_t board_usb_test_error_count;

static void board_usb_test_count_error(void) {
    if (board_usb_test_error_count != UINT32_MAX) {
        ++board_usb_test_error_count;
    }
}

static uint8_t board_usb_test_expected_byte(uint32_t packet_index,
                                            uint32_t packet_offset) {
    uint32_t value;

    value = 0x4E415441UL;
    value ^= packet_index * 0x01010101UL;
    value ^= packet_offset * 0x0001003DUL;
    value ^= board_usb_test_acquisition_period_ticks;
    value ^= value >> 16U;
    value ^= value >> 8U;

    return (uint8_t)(value & 0xFFU);
}

static void board_usb_test_capture_check(const uint8_t* buffer, size_t size) {
    size_t index;
    uint32_t absolute_offset;
    uint32_t packet_index;
    uint32_t packet_offset;
    uint32_t expected_total_bytes;
    uint8_t expected;

    expected_total_bytes = board_usb_test_expected_packet_count *
        BOARD_USB_TEST_PACKET_SIZE;

    for (index = 0U; index < size; ++index) {
        if (board_usb_test_bytes_written >= expected_total_bytes) {
            board_usb_test_count_error();
            continue;
        }

        absolute_offset = board_usb_test_bytes_written;
        packet_index = absolute_offset / BOARD_USB_TEST_PACKET_SIZE;
        packet_offset = absolute_offset % BOARD_USB_TEST_PACKET_SIZE;

        expected = board_usb_test_expected_byte(packet_index, packet_offset);

        if (buffer[index] != expected) {
            board_usb_test_count_error();
        }

        ++board_usb_test_bytes_written;
    }
}

BoardStatus board_usb_test_capture_start(uint32_t packet_count,
                                         uint32_t acquisition_period_ticks) {
    if (packet_count == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (packet_count > (UINT32_MAX / BOARD_USB_TEST_PACKET_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_usb_test_capture_enabled = 1U;
    board_usb_test_expected_packet_count = packet_count;
    board_usb_test_acquisition_period_ticks = acquisition_period_ticks;
    board_usb_test_bytes_written = 0U;
    board_usb_test_error_count = 0U;

    return BOARD_OK;
}

BoardStatus board_usb_test_capture_get_result(uint32_t* bytes_written,
                                              uint32_t* expected_bytes,
                                              uint32_t* error_count) {
    if ((bytes_written == 0) || (expected_bytes == 0) || (error_count == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = board_usb_test_bytes_written;
    *expected_bytes = board_usb_test_expected_packet_count *
        BOARD_USB_TEST_PACKET_SIZE;
    *error_count = board_usb_test_error_count;

    return BOARD_OK;
}

BoardStatus board_usb_write(const void* buffer, size_t size, size_t* bytes_written) {
    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_written == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_usb_test_capture_enabled != 0U) {
        board_usb_test_capture_check(buffer, size);
    }

    *bytes_written = size;

    return BOARD_OK;
}

BoardStatus board_usb_is_ready(uint8_t* is_ready) {
    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 1U;

    return BOARD_OK;
}

#elif defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && \
      (NATALIA_ENABLE_USB_DEVICE_DRIVER != 0)

BoardStatus board_usb_write(const void* buffer, size_t size, size_t* bytes_written) {
    return usb_cdc_write(buffer, size, bytes_written);
}

BoardStatus board_usb_is_ready(uint8_t* is_ready) {
    return usb_cdc_is_ready(is_ready);
}

#else

BoardStatus board_usb_write(const void* buffer, size_t size, size_t* bytes_written) {
    (void)buffer;
    (void)size;

    if (bytes_written == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = 0U;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_usb_is_ready(uint8_t* is_ready) {
    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 0U;

    return BOARD_ERR_UNSUPPORTED;
}

#endif

#if defined(NATALIA_ENABLE_FTDI_DRIVER) && (NATALIA_ENABLE_FTDI_DRIVER != 0)

BoardStatus board_data_write(const void* buffer, size_t size, size_t* bytes_written) {
    size_t accepted;
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_written == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    ftdi_set_mode(FTDI_MODE_DATA);

    status = ftdi_write((const uint8_t*)buffer, size, &accepted);
    if (status != BOARD_OK) {
        *bytes_written = 0U;
        return status;
    }

    *bytes_written = accepted;

    return BOARD_OK;
}

BoardStatus board_data_is_ready(uint8_t* is_ready) {
    return ftdi_is_tx_idle(is_ready);
}

#else

BoardStatus board_data_write(const void* buffer, size_t size, size_t* bytes_written) {
    return board_usb_write(buffer, size, bytes_written);
}

BoardStatus board_data_is_ready(uint8_t* is_ready) {
    return board_usb_is_ready(is_ready);
}

#endif

static BoardStatus board_status_read_output_bit(uint32_t* power_status,
                                                BoardPinId pin,
                                                uint32_t bit_index) {
    GpioLevel level;
    BoardStatus status;

    status = gpio_read_output_latch(pin, &level);
    if (status != BOARD_OK) {
        return status;
    }

    if (level == GPIO_LEVEL_HIGH) {
        *power_status |= 1UL << bit_index;
    }

    return BOARD_OK;
}

static BoardStatus board_status_read_input_bit(uint32_t* power_status,
                                               BoardPinId pin,
                                               uint32_t bit_index) {
    GpioLevel level;
    BoardStatus status;

    status = gpio_read(pin, &level);
    if (status != BOARD_OK) {
        return status;
    }

    if (level == GPIO_LEVEL_HIGH) {
        *power_status |= 1UL << bit_index;
    }

    return BOARD_OK;
}

static void board_power_clear_sample(BoardPowerSample* sample) {
    if (sample != 0) {
        sample->bus_voltage_mv = 0U;
        sample->shunt_voltage_uv = 0;
        sample->current_ua = 0;
        sample->power_uw = 0U;
        sample->conversion_ready = 0U;
        sample->math_overflow = 0U;
        sample->ready = 0U;
    }
}

BoardStatus board_power_monitor_init(void) {
#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
    const Ina219Config* pu_config;
    const Ina219Config* ped_config;
    BoardStatus status;

    board_ina219_pu_initialized = 0U;
    board_ina219_ped_initialized = 0U;

    status = ina219_config_get(INA219_CONFIG_CHANNEL_PU, &pu_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_config_get(INA219_CONFIG_CHANNEL_PED, &ped_config);
    if (status != BOARD_OK) {
        return status;
    }

    status = ina219_init(&board_ina219_pu_device, pu_config);
    if (status != BOARD_OK) {
        return status;
    }

    board_ina219_pu_initialized = 1U;

    status = ina219_init(&board_ina219_ped_device, ped_config);
    if (status != BOARD_OK) {
        board_ina219_pu_initialized = 0U;
        return status;
    }

    board_ina219_ped_initialized = 1U;

    return BOARD_OK;
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_power_monitor(BoardPowerMonitorId monitor,
                                     BoardPowerSample* sample) {
#if defined(NATALIA_ENABLE_INA219_DRIVER) && (NATALIA_ENABLE_INA219_DRIVER != 0)
    Ina219Sample ina_sample;
    Ina219Device* device;
    uint8_t initialized;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_power_clear_sample(sample);

    if (monitor == BOARD_POWER_MONITOR_PU) {
        device = &board_ina219_pu_device;
        initialized = board_ina219_pu_initialized;
    } else if (monitor == BOARD_POWER_MONITOR_PED) {
        device = &board_ina219_ped_device;
        initialized = board_ina219_ped_initialized;
    } else {
        return BOARD_ERR_INVALID_ARG;
    }

    if (initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = ina219_read_sample(device, &ina_sample);
    if (status != BOARD_OK) {
        return status;
    }

    sample->bus_voltage_mv = ina_sample.bus_voltage_mv;
    sample->shunt_voltage_uv = ina_sample.shunt_voltage_uv;
    sample->current_ua = ina_sample.current_ua;
    sample->power_uw = ina_sample.power_uw;
    sample->conversion_ready = ina_sample.conversion_ready;
    sample->math_overflow = ina_sample.math_overflow;
    sample->ready = 1U;

    return BOARD_OK;
#else
    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_power_clear_sample(sample);

    (void)monitor;

    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_power_status(uint32_t* power_status) {
    BoardStatus status;

    if (power_status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *power_status = 0U;

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_CAN1_SHDN, 0U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_CAN1_S, 1U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_CAN2_SHDN, 2U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_CAN2_S, 3U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_NAND1_PS, 4U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_input_bit(power_status, BOARD_PIN_PU_NAND1_PSON, 5U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_NAND2_PS, 6U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_input_bit(power_status, BOARD_PIN_PU_NAND2_PSON, 7U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PU_PED_PS, 8U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_input_bit(power_status, BOARD_PIN_PU_USB_VBUS, 9U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PED_INHIBIT, 13U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_output_bit(power_status, BOARD_PIN_PED_SLEEP, 14U);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_status_read_input_bit(power_status, BOARD_PIN_PED_PSON, 15U);
    if (status != BOARD_OK) {
        return status;
    }

    return BOARD_OK;
}


#include "board_api.h"

#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
#include "tc1047.h"
#endif

#if defined(NATALIA_ENABLE_TMP112_DRIVER) && (NATALIA_ENABLE_TMP112_DRIVER != 0)
#include "tmp112.h"
#include "tmp112_config.h"

static uint8_t board_tmp112_initialized;
#endif

static void board_digital_temp_clear_sample(BoardDigitalTempSample* sample) {
    if (sample != 0) {
        sample->temperature_milli_c = 0;
        sample->raw_12bit = 0;
        sample->ready = 0U;
        sample->range_valid = 0U;
    }
}

#if defined(NATALIA_ENABLE_TMP112_DRIVER) && (NATALIA_ENABLE_TMP112_DRIVER != 0)
static uint8_t board_digital_temp_address(BoardTempSensorId sensor, uint8_t* address) {
    switch (sensor) {
    case BOARD_TEMP_SENSOR_PU:
        *address = TMP112_CONFIG_PU_ADDRESS_7BIT;
        return 1U;
    case BOARD_TEMP_SENSOR_PED:
        *address = TMP112_CONFIG_PED_ADDRESS_7BIT;
        return 1U;
    default:
        return 0U;
    }
}
#endif

static void board_temp_clear_sample(BoardTempSample* sample) {
    if (sample != 0) {
        sample->temperature_milli_c = 0;
        sample->millivolts = 0U;
        sample->vdda_mv = 0U;
        sample->adc_sequence = 0U;
        sample->raw = 0U;
        sample->vrefint_raw = 0U;
        sample->ready = 0U;
        sample->range_valid = 0U;
    }
}

BoardStatus board_temp_init(void) {
#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
    return tc1047_init();
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_temp_start(void) {
#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
    return tc1047_start();
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_temp_stop(void) {
#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
    return tc1047_stop();
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_temp(BoardTempSample* sample) {
#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
    Tc1047Sample tc_sample;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_temp_clear_sample(sample);

    status = tc1047_read(&tc_sample);
    if (status != BOARD_OK) {
        return status;
    }

    sample->temperature_milli_c = tc_sample.temperature_milli_c;
    sample->millivolts = tc_sample.millivolts;
    sample->vdda_mv = tc_sample.vdda_mv;
    sample->adc_sequence = tc_sample.adc_sequence;
    sample->raw = tc_sample.raw;
    sample->vrefint_raw = tc_sample.vrefint_raw;
    sample->ready = tc_sample.ready;
    sample->range_valid = tc_sample.range_valid;

    return BOARD_OK;
#else
    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_temp_clear_sample(sample);

    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_temp_milli_c(int32_t* temperature_milli_c) {
#if defined(NATALIA_ENABLE_TC1047_DRIVER) && (NATALIA_ENABLE_TC1047_DRIVER != 0)
    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    return tc1047_read_temperature_milli_c(temperature_milli_c);
#else
    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *temperature_milli_c = 0;

    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_temp_digital_init(void) {
#if defined(NATALIA_ENABLE_TMP112_DRIVER) && (NATALIA_ENABLE_TMP112_DRIVER != 0)
    BoardStatus status;

    board_tmp112_initialized = 0U;

    status = tmp112_init(TMP112_CONFIG_SPEED);
    if (status != BOARD_OK) {
        return status;
    }

    board_tmp112_initialized = 1U;

    return BOARD_OK;
#else
    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_digital_temp(BoardTempSensorId sensor, BoardDigitalTempSample* sample) {
#if defined(NATALIA_ENABLE_TMP112_DRIVER) && (NATALIA_ENABLE_TMP112_DRIVER != 0)
    Tmp112Sample tmp_sample;
    uint8_t address;
    BoardStatus status;

    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_digital_temp_clear_sample(sample);

    if (board_digital_temp_address(sensor, &address) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_tmp112_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    status = tmp112_read_sample(address, &tmp_sample);
    if (status != BOARD_OK) {
        return status;
    }

    sample->temperature_milli_c = tmp_sample.temperature_milli_c;
    sample->raw_12bit = tmp_sample.raw_12bit;
    sample->range_valid = tmp_sample.range_valid;
    sample->ready = 1U;

    return BOARD_OK;
#else
    if (sample == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_digital_temp_clear_sample(sample);

    (void)sensor;

    return BOARD_ERR_UNSUPPORTED;
#endif
}

BoardStatus board_read_digital_temp_milli_c(BoardTempSensorId sensor, int32_t* temperature_milli_c) {
#if defined(NATALIA_ENABLE_TMP112_DRIVER) && (NATALIA_ENABLE_TMP112_DRIVER != 0)
    uint8_t address;

    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_digital_temp_address(sensor, &address) == 0U) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_tmp112_initialized == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    return tmp112_read_temperature_milli_c(address, temperature_milli_c);
#else
    if (temperature_milli_c == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *temperature_milli_c = 0;

    (void)sensor;

    return BOARD_ERR_UNSUPPORTED;
#endif
}

#if defined(NATALIA_ENABLE_UNICAN_DRIVER) && (NATALIA_ENABLE_UNICAN_DRIVER != 0)

_Static_assert(BOARD_COMM_MAX_MESSAGE_DATA <= UNICAN_MAX_MESSAGE_DATA,
               "board_comm forwards length straight to unican_send; "
               "BOARD_COMM_MAX_MESSAGE_DATA must not exceed UNICAN_MAX_MESSAGE_DATA");

BoardStatus board_comm_init(void) {
    BoardStatus status;

    status = can1_init();
    if (status != BOARD_OK) {
        return status;
    }

    unican_init();

    return BOARD_OK;
}

void board_comm_close(void) {
    unican_close();
}

void board_comm_poll(uint32_t now_ms) {
    unican_poll(now_ms);
}

BoardStatus board_comm_send(const BoardCommMessage* message) {
    UnicanMessage unican_message;

    if (message == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    unican_message.message_id = message->message_id;
    unican_message.address_from = message->address_from;
    unican_message.address_to = message->address_to;
    unican_message.length = message->length;
    unican_message.data = message->data;

    return unican_send(&unican_message);
}

BoardStatus board_comm_receive(BoardCommMessage* message, uint8_t* buffer, uint16_t capacity) {
    UnicanMessage unican_message;
    BoardStatus status;

    if (message == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = unican_receive(&unican_message, buffer, capacity);
    if (status != BOARD_OK) {
        return status;
    }

    message->message_id = unican_message.message_id;
    message->address_from = unican_message.address_from;
    message->address_to = unican_message.address_to;
    message->length = unican_message.length;
    message->data = unican_message.data;

    return BOARD_OK;
}

void board_comm_get_status(BoardCommStatus* status) {
    UnicanStatus unican_status;

    if (status == NULL) {
        return;
    }

    unican_get_status(&unican_status);

    status->is_online = unican_status.is_online;
    status->tx_busy = unican_status.tx_busy;
    status->tx_messages_ok = unican_status.tx_messages_ok;
    status->tx_messages_failed = unican_status.tx_messages_failed;
}

#else /* UniCAN driver disabled */

BoardStatus board_comm_init(void) {
    return BOARD_ERR_UNSUPPORTED;
}

void board_comm_close(void) {
}

void board_comm_poll(uint32_t now_ms) {
    (void)now_ms;
}

BoardStatus board_comm_send(const BoardCommMessage* message) {
    (void)message;
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_comm_receive(BoardCommMessage* message, uint8_t* buffer, uint16_t capacity) {
    (void)message;
    (void)buffer;
    (void)capacity;
    return BOARD_ERR_NOT_READY;
}

void board_comm_get_status(BoardCommStatus* status) {
    if (status == NULL) {
        return;
    }

    status->is_online = false;
    status->tx_busy = false;
    status->tx_messages_ok = 0U;
    status->tx_messages_failed = 0U;
}

#endif /* NATALIA_ENABLE_UNICAN_DRIVER */
