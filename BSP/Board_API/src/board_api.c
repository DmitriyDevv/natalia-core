#include "board_api.h"

#include <string.h>

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

#define BOARD_MRAM_COPY_COUNT 2U
#define BOARD_MRAM_COPY_SIZE 1024U

static uint8_t board_mram_stub_storage[BOARD_MRAM_COPY_COUNT][BOARD_MRAM_COPY_SIZE];
static uint8_t board_mram_stub_initialized;

#if defined(NATALIA_ENABLE_NAND_DRIVER)
static uint8_t board_nand_storage_initialized;
static uint8_t board_nand_active_bank_id;
#endif

BoardStatus board_init_hardware(void) {
    BoardStatus status;

#if defined(NATALIA_ENABLE_NAND_DRIVER)
    board_nand_storage_initialized = 0U;
    board_nand_active_bank_id = 0U;
#endif

    status = board_startup_io_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = rtc_init();
    if (status != BOARD_OK) {
        return status;
    }

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

    return status;
}

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

    status = gpio_set_disconnected(BOARD_PIN_QSPI_CLK);
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
#if (NATALIA_NAND_POWER_CONTROL == 0)
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
#if (NATALIA_NAND_POWER_CONTROL == 0)
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
#if (NATALIA_NAND_POWER_CONTROL == 0)
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

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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
    (void)is_idle;

    return BOARD_ERR_UNSUPPORTED;
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
    (void)is_done;

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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
    (void)has_packet;

    return BOARD_ERR_UNSUPPORTED;
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
    (void)packet_capacity;

    return BOARD_ERR_UNSUPPORTED;
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
    (void)packet_count;

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
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
    (void)is_done;

    return BOARD_ERR_UNSUPPORTED;
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
    (void)is_full;

    return BOARD_ERR_UNSUPPORTED;
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

#else

BoardStatus board_ped_power_on(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_is_powered(uint8_t* is_powered) {
    if (is_powered == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = 0U;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_read_status(uint32_t* status) {
    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *status = 0U;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_write_config(const void* config, size_t size) {
    (void)config;
    (void)size;

    return BOARD_ERR_UNSUPPORTED;
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

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_set_inhibit(uint8_t enabled) {
    (void)enabled;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_set_sleep(uint8_t enabled) {
    (void)enabled;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_ped_reset_trigger(void) {
    return BOARD_ERR_UNSUPPORTED;
}

#endif

BoardStatus board_rtc_get_time(InstrumentTime* time) {
    return rtc_get_time(time);
}

BoardStatus board_rtc_set_time(const InstrumentTime* time) {
    return rtc_set_time(time);
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

static void board_usb_test_capture_check(const uint8_t *buffer, size_t size) {
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

BoardStatus board_usb_test_capture_get_result(uint32_t *bytes_written,
                                              uint32_t *expected_bytes,
                                              uint32_t *error_count) {
    if ((bytes_written == 0) || (expected_bytes == 0) || (error_count == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = board_usb_test_bytes_written;
    *expected_bytes = board_usb_test_expected_packet_count *
        BOARD_USB_TEST_PACKET_SIZE;
    *error_count = board_usb_test_error_count;

    return BOARD_OK;
}

BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written) {
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

BoardStatus board_usb_is_ready(uint8_t *is_ready) {
    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 1U;

    return BOARD_OK;
}

#elif defined(NATALIA_ENABLE_USB_DEVICE_DRIVER) && \
      (NATALIA_ENABLE_USB_DEVICE_DRIVER != 0)

BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written) {
    return usb_cdc_write(buffer, size, bytes_written);
}

BoardStatus board_usb_is_ready(uint8_t *is_ready) {
    return usb_cdc_is_ready(is_ready);
}

#else

BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written) {
    (void)buffer;
    (void)size;

    if (bytes_written == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = 0U;

    return BOARD_ERR_UNSUPPORTED;
}

BoardStatus board_usb_is_ready(uint8_t *is_ready) {
    if (is_ready == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 0U;

    return BOARD_ERR_UNSUPPORTED;
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
