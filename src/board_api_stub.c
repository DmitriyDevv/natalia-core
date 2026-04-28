#include "board_api.h"

BoardStatus board_init_hardware(void) {
    return BOARD_OK;
}

BoardStatus board_enter_safe_config(void) {
    return BOARD_OK;
}

BoardStatus board_disconnect_signal_lines(BoardSignalTarget target) {
    (void)target;
    return BOARD_OK;
}

BoardStatus board_mram_read(uint8_t copy_id, uint32_t offset, void *buffer, size_t size) {
    (void)copy_id;
    (void)offset;
    (void)buffer;
    (void)size;
    return BOARD_OK;
}

BoardStatus board_mram_write(uint8_t copy_id, uint32_t offset, const void *buffer, size_t size) {
    (void)copy_id;
    (void)offset;
    (void)buffer;
    (void)size;
    return BOARD_OK;
}

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t *is_valid) {
    (void)copy_id;
    if (is_valid != NULL) {
        *is_valid = 1U;
    }
    return BOARD_OK;
}

BoardStatus board_mram_restore_copy(uint8_t source_copy_id, uint8_t target_copy_id) {
    (void)source_copy_id;
    (void)target_copy_id;
    return BOARD_OK;
}

BoardStatus board_nand_power_on(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_OK;
}

BoardStatus board_nand_power_off(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_OK;
}

BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t *is_powered) {
    (void)bank_id;
    if (is_powered != NULL) {
        *is_powered = 1U;
    }
    return BOARD_OK;
}

BoardStatus board_nand_connect(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_OK;
}

BoardStatus board_nand_disconnect(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_OK;
}

BoardStatus board_nand_read(uint8_t bank_id, uint32_t address, void *buffer, size_t size) {
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;
    return BOARD_OK;
}

BoardStatus board_nand_write(uint8_t bank_id, uint32_t address, const void *buffer, size_t size) {
    (void)bank_id;
    (void)address;
    (void)buffer;
    (void)size;
    return BOARD_OK;
}

BoardStatus board_nand_erase_start(uint8_t bank_id) {
    (void)bank_id;
    return BOARD_OK;
}

BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t *is_done) {
    (void)bank_id;
    if (is_done != NULL) {
        *is_done = 1U;
    }
    return BOARD_OK;
}

BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t *is_full) {
    (void)bank_id;
    if (is_full != NULL) {
        *is_full = 0U;
    }
    return BOARD_OK;
}

BoardStatus board_ped_power_on(void) {
    return BOARD_OK;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_OK;
}

BoardStatus board_ped_is_powered(uint8_t *is_powered) {
    if (is_powered != NULL) {
        *is_powered = 1U;
    }
    return BOARD_OK;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_OK;
}

BoardStatus board_ped_read_status(uint32_t *status) {
    if (status != NULL) {
        *status = 0U;
    }
    return BOARD_OK;
}

BoardStatus board_ped_write_config(const void *config, size_t size) {
    (void)config;
    (void)size;
    return BOARD_OK;
}

BoardStatus board_ped_read_event(void *event_buffer, size_t buffer_size, size_t *bytes_read) {
    (void)event_buffer;
    (void)buffer_size;
    if (bytes_read != NULL) {
        *bytes_read = 0U;
    }
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

BoardStatus board_rtc_get_time(uint64_t *time_ticks) {
    if (time_ticks != NULL) {
        *time_ticks = 0U;
    }
    return BOARD_OK;
}

BoardStatus board_rtc_set_time(uint64_t time_ticks) {
    (void)time_ticks;
    return BOARD_OK;
}

BoardStatus board_can_receive(uint32_t *message_id, uint8_t *payload, size_t payload_capacity, size_t *payload_size) {
    (void)payload;
    (void)payload_capacity;
    if (message_id != NULL) {
        *message_id = 0U;
    }
    if (payload_size != NULL) {
        *payload_size = 0U;
    }
    return BOARD_OK;
}

BoardStatus board_can_send(uint32_t message_id, const uint8_t *payload, size_t payload_size) {
    (void)message_id;
    (void)payload;
    (void)payload_size;
    return BOARD_OK;
}

BoardStatus board_can_send_ack(uint16_t command_id, BoardAckStatus status) {
    (void)command_id;
    (void)status;
    return BOARD_OK;
}

BoardStatus board_can_send_status(void) {
    return BOARD_OK;
}

BoardStatus board_can_send_telemetry(void) {
    return BOARD_OK;
}

BoardStatus board_can_send_test_result(void) {
    return BOARD_OK;
}

BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written) {
    (void)buffer;
    if (bytes_written != NULL) {
        *bytes_written = size;
    }
    return BOARD_OK;
}

BoardStatus board_usb_is_ready(uint8_t *is_ready) {
    if (is_ready != NULL) {
        *is_ready = 1U;
    }
    return BOARD_OK;
}

BoardStatus board_read_power_status(uint32_t *power_status) {
    if (power_status != NULL) {
        *power_status = 0U;
    }
    return BOARD_OK;
}
