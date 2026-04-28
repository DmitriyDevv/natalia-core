#ifndef NATALIA_CORE_BOARD_API_H
#define NATALIA_CORE_BOARD_API_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOARD_OK = 0,
    BOARD_ERR_INVALID_ARG,
    BOARD_ERR_TIMEOUT,
    BOARD_ERR_IO,
    BOARD_ERR_CRC,
    BOARD_ERR_BUSY,
    BOARD_ERR_NOT_READY,
    BOARD_ERR_UNSUPPORTED
} BoardStatus;

typedef enum {
    BOARD_ACK_OK = 0,
    BOARD_ACK_ERR_MODE,
    BOARD_ACK_ERR_CONTENT,
    BOARD_ACK_ERR_OTHER
} BoardAckStatus;

typedef enum {
    BOARD_SIGNAL_NAND1 = 0,
    BOARD_SIGNAL_NAND2,
    BOARD_SIGNAL_PED,
    BOARD_SIGNAL_RTC_OUT
} BoardSignalTarget;

/* Common board state and safe hardware configuration. */
BoardStatus board_init_hardware(void);

BoardStatus board_enter_safe_config(void);

BoardStatus board_disconnect_signal_lines(BoardSignalTarget target);

/* MRAM access for configuration and service data copies. */
BoardStatus board_mram_read(uint8_t copy_id, uint32_t offset, void *buffer, size_t size);

BoardStatus board_mram_write(uint8_t copy_id, uint32_t offset, const void *buffer, size_t size);

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t *is_valid);

BoardStatus board_mram_restore_copy(uint8_t source_copy_id, uint8_t target_copy_id);

/* NAND bank power, connection, data access, erase, and capacity checks. */
BoardStatus board_nand_power_on(uint8_t bank_id);

BoardStatus board_nand_power_off(uint8_t bank_id);

BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t *is_powered);

BoardStatus board_nand_connect(uint8_t bank_id);

BoardStatus board_nand_disconnect(uint8_t bank_id);

BoardStatus board_nand_read(uint8_t bank_id, uint32_t address, void *buffer, size_t size);

BoardStatus board_nand_write(uint8_t bank_id, uint32_t address, const void *buffer, size_t size);

BoardStatus board_nand_erase_start(uint8_t bank_id);

BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t *is_done);

BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t *is_full);

/* PED power, PED_REG access, control lines, and event readout. */
BoardStatus board_ped_power_on(void);

BoardStatus board_ped_power_off(void);

BoardStatus board_ped_is_powered(uint8_t *is_powered);

BoardStatus board_ped_reg_init(void);

BoardStatus board_ped_read_status(uint32_t *status);

BoardStatus board_ped_write_config(const void *config, size_t size);

BoardStatus board_ped_read_event(void *event_buffer, size_t buffer_size, size_t *bytes_read);

BoardStatus board_ped_set_inhibit(uint8_t enabled);

BoardStatus board_ped_set_sleep(uint8_t enabled);

BoardStatus board_ped_reset_trigger(void);

/* RTC time source and time preset. */
BoardStatus board_rtc_get_time(uint64_t *time_ticks);

BoardStatus board_rtc_set_time(uint64_t time_ticks);

/* CAN/UniCAN receive and transmit primitives plus standard telemetry packets. */
BoardStatus board_can_receive(uint32_t *message_id, uint8_t *payload, size_t payload_capacity, size_t *payload_size);

BoardStatus board_can_send(uint32_t message_id, const uint8_t *payload, size_t payload_size);

BoardStatus board_can_send_ack(uint16_t command_id, BoardAckStatus status);

BoardStatus board_can_send_status(void);

BoardStatus board_can_send_telemetry(void);

BoardStatus board_can_send_test_result(void);

/* USB output used by DUMP mode. */
BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written);

BoardStatus board_usb_is_ready(uint8_t *is_ready);

/* Board monitoring inputs used to form alarms and status telemetry. */
BoardStatus board_read_power_status(uint32_t *power_status);

#endif // NATALIA_CORE_BOARD_API_H
