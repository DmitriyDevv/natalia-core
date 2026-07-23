#ifndef NATALIA_CORE_BOARD_API_H
#define NATALIA_CORE_BOARD_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "status.h"
#include "instrument_time.h"

typedef enum {
    BOARD_SIGNAL_NAND1 = 0,
    BOARD_SIGNAL_NAND2,
    BOARD_SIGNAL_PED,
    BOARD_SIGNAL_RTC_OUT
} BoardSignalTarget;

typedef struct {
    int32_t temperature_milli_c;
    uint32_t millivolts;
    uint32_t vdda_mv;
    uint32_t adc_sequence;
    uint16_t raw;
    uint16_t vrefint_raw;
    uint8_t ready;
    uint8_t range_valid;
} BoardTempSample;

typedef enum {
    BOARD_POWER_MONITOR_PU = 0,
    BOARD_POWER_MONITOR_PED
} BoardPowerMonitorId;

typedef struct {
    uint32_t bus_voltage_mv;
    int32_t shunt_voltage_uv;
    int32_t current_ua;
    uint32_t power_uw;
    uint8_t conversion_ready;
    uint8_t math_overflow;
    uint8_t ready;
} BoardPowerSample;

typedef enum {
    BOARD_TEMP_SENSOR_PU = 0,
    BOARD_TEMP_SENSOR_PED
} BoardTempSensorId;

typedef struct {
    int32_t temperature_milli_c;
    int16_t raw_12bit;
    uint8_t ready;
    uint8_t range_valid;
} BoardDigitalTempSample;

BoardStatus board_init_hardware(void);
BoardStatus board_enter_safe_config(void);
BoardStatus board_disconnect_signal_lines(BoardSignalTarget target);

BoardStatus board_mram_read(uint8_t copy_id, uint32_t offset, void* buffer, size_t size);
BoardStatus board_mram_write(uint8_t copy_id, uint32_t offset, const void* buffer, size_t size);
BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t* is_valid);
BoardStatus board_mram_restore_copy(uint8_t source_copy_id, uint8_t target_copy_id);

BoardStatus board_nand_power_on(uint8_t bank_id);
BoardStatus board_nand_power_off(uint8_t bank_id);
BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t* is_powered);
BoardStatus board_nand_connect(uint8_t bank_id);
BoardStatus board_nand_disconnect(uint8_t bank_id);

BoardStatus board_nand_read(uint8_t bank_id, uint32_t address, void* buffer, size_t size);
BoardStatus board_nand_write(uint8_t bank_id, uint32_t address, const void* buffer, size_t size);

BoardStatus board_nand_open_write(uint8_t bank_id, uint32_t start_packet_count);
BoardStatus board_nand_write_packet(uint8_t bank_id, const void* packet);
BoardStatus board_nand_write_poll(uint8_t bank_id, uint8_t* is_idle);
BoardStatus board_nand_write_flush(uint8_t bank_id, uint8_t* is_done);

BoardStatus board_nand_open_read(uint8_t bank_id, uint32_t packet_count);
BoardStatus board_nand_read_packet(uint8_t bank_id, uint32_t packet_index, void* packet);
BoardStatus board_nand_read_next_packet(uint8_t bank_id, void* packet, uint8_t* has_packet);

BoardStatus board_nand_get_capacity_packets(uint8_t bank_id, uint32_t* packet_capacity);
BoardStatus board_nand_get_committed_packet_count(uint8_t bank_id, uint32_t* packet_count);

BoardStatus board_nand_erase_start(uint8_t bank_id);
BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t* is_done);
BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t* is_full);

BoardStatus board_ped_power_on(void);
BoardStatus board_ped_power_off(void);
BoardStatus board_ped_is_powered(uint8_t* is_powered);
BoardStatus board_ped_reg_init(void);
BoardStatus board_ped_read_status(uint32_t* status);
BoardStatus board_ped_write_config(const void* config, size_t size);
BoardStatus board_ped_read_event(void* event_buffer, size_t buffer_size, size_t* bytes_read);
BoardStatus board_ped_set_inhibit(uint8_t enabled);
BoardStatus board_ped_set_sleep(uint8_t enabled);
BoardStatus board_ped_reset_trigger(void);
BoardStatus board_ped_take_trigger_events(uint32_t* event_count);

BoardStatus board_rtc_get_time(InstrumentTime* time);
BoardStatus board_rtc_set_time(const InstrumentTime* time);
BoardStatus board_rtc_take_1hz_events(uint32_t* event_count);

BoardStatus board_temp_init(void);
BoardStatus board_temp_start(void);
BoardStatus board_temp_stop(void);
BoardStatus board_read_temp(BoardTempSample* sample);
BoardStatus board_read_temp_milli_c(int32_t* temperature_milli_c);

BoardStatus board_temp_digital_init(void);
BoardStatus board_read_digital_temp(BoardTempSensorId sensor, BoardDigitalTempSample* sample);
BoardStatus board_read_digital_temp_milli_c(BoardTempSensorId sensor, int32_t* temperature_milli_c);

BoardStatus board_usb_write(const void* buffer, size_t size, size_t* bytes_written);
BoardStatus board_usb_is_ready(uint8_t* is_ready);

BoardStatus board_data_write(const void* buffer, size_t size, size_t* bytes_written);
BoardStatus board_data_is_ready(uint8_t* is_ready);

#if defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) && (NATALIA_ENABLE_BOARD_TEST_HOOKS != 0)
BoardStatus board_usb_test_capture_start(uint32_t packet_count, uint32_t acquisition_period_ticks);
BoardStatus board_usb_test_capture_get_result(uint32_t* bytes_written, uint32_t* expected_bytes, uint32_t* error_count);
#endif

BoardStatus board_read_power_status(uint32_t* power_status);
BoardStatus board_power_monitor_init(void);
BoardStatus board_read_power_monitor(BoardPowerMonitorId monitor, BoardPowerSample* sample);

#define BOARD_COMM_MAX_MESSAGE_DATA (6144U)
#define BOARD_COMM_ADDR_NA          (0x1EU)
#define BOARD_COMM_ADDR_BVS         (0x05U)

typedef struct {
    uint16_t message_id;
    uint16_t address_from;
    uint16_t address_to;
    uint16_t length;
    const uint8_t* data;
} BoardCommMessage;

typedef struct {
    bool is_online;
    bool tx_busy;
    uint32_t tx_messages_ok;
    uint32_t tx_messages_failed;
} BoardCommStatus;

BoardStatus board_comm_init(void);
void board_comm_close(void);
void board_comm_poll(uint32_t now_ms);
BoardStatus board_comm_send(const BoardCommMessage* message);
BoardStatus board_comm_receive(BoardCommMessage* message, uint8_t* buffer, uint16_t capacity);
void board_comm_get_status(BoardCommStatus* status);

#endif
