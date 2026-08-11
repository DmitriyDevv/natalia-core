#ifndef NATALIA_CORE_MRAM_STORE_H
#define NATALIA_CORE_MRAM_STORE_H

#include <stdint.h>

#include "../../BSP/Board_API/include/board_api.h"
#include "alarm.h"
#include "test_mode_config.h"

#define MRAM_STORE_ALARM_BOTH_COPIES_INVALID ALARM_MRAM

/*
 * Persisted configuration. Field set and 16-bit widths follow the CMD_SET_CFG
 * payload (Протокол_CAN_ГС_v2). Threshold/belt/rate units are pinned by the
 * alarm-monitoring phase; only the field widths matter for the stored layout.
 */
typedef struct {
    int16_t mcu_pu_temp_min;
    int16_t mcu_pu_temp_max;
    int16_t pu_temp_min;
    int16_t pu_temp_max;
    int16_t ped_temp_min;
    int16_t ped_temp_max;
    int16_t det_temp_min;
    int16_t det_temp_max;
    uint16_t pu_voltage_min;
    uint16_t pu_voltage_max;
    uint16_t pu_current_min;
    uint16_t pu_current_max;
    uint16_t ped_voltage_min;
    uint16_t ped_voltage_max;
    uint16_t ped_current_min;
    uint16_t ped_current_max;
    int16_t belt_lmin;
    int16_t belt_lmax;
    int16_t belt_bmin;
    uint16_t ac1_rate_max;
    uint32_t init_rtc_time;
    uint16_t can_control;
    uint16_t alarm_mask;
    uint16_t config_version;
    uint16_t init_rtc_time_ms;
    uint16_t destination_id;
    uint16_t device_id;
} MramStoreConfig;

/*
 * Persisted service data: runtime status plus firmware-maintained service
 * counters (CMD_SET_CFG can preset the counters selectively via its write-
 * control word; the increment logic itself lands in the mode phases).
 */
typedef struct {
    uint16_t alarm_status;
    uint8_t nand1_full;
    uint8_t nand2_full;
    uint32_t last_test_status;
    uint16_t observe_session_id;
    uint32_t nand1_packet_count;
    uint32_t nand2_packet_count;
    uint16_t nand1_erase_count;
    uint16_t nand2_erase_count;
    uint16_t nand1_test_count;
    uint16_t nand2_test_count;
    uint32_t nand1_last_dumped_packet;
    uint32_t nand2_last_dumped_packet;
} MramStoreServiceData;

typedef struct {
    uint8_t bank;
    uint32_t nerr[TEST_MODE_BLOCK_COUNT];
} MramStoreTestResult;

typedef struct {
    uint8_t copy1_valid;
    uint8_t copy2_valid;
} MramStoreStatus;

BoardStatus mram_store_check(MramStoreStatus *status);

BoardStatus mram_store_restore_redundant_copy(void);

BoardStatus mram_store_load_config(MramStoreConfig *config);

BoardStatus mram_store_save_config(const MramStoreConfig *config);

BoardStatus mram_store_save_addresses(uint16_t device_id, uint16_t destination_id);

BoardStatus mram_store_load_service_data(MramStoreServiceData *service_data);

BoardStatus mram_store_save_service_data(const MramStoreServiceData *service_data);

BoardStatus mram_store_save_test_result(const MramStoreTestResult *test_result);

BoardStatus mram_store_load_test_result(uint8_t bank, MramStoreTestResult *test_result);

#endif // NATALIA_CORE_MRAM_STORE_H
