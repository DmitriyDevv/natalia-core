#ifndef NATALIA_CORE_MRAM_STORE_H
#define NATALIA_CORE_MRAM_STORE_H

#include <stdint.h>

#include "board_api.h"
#include "alarm.h"

#define MRAM_STORE_ALARM_BOTH_COPIES_INVALID ALARM_MRAM

typedef struct {
    uint32_t alarm_mask;
    uint32_t config_version;
} MramStoreConfig;

typedef struct {
    uint32_t alarm_status;
    uint8_t nand1_full;
    uint8_t nand2_full;
    uint32_t last_test_status;
} MramStoreServiceData;

typedef struct {
    uint8_t bank;
    uint32_t status;
    uint32_t failed_address;
} MramStoreTestResult;

typedef struct {
    uint8_t copy1_valid;
    uint8_t copy2_valid;
} MramStoreStatus;

BoardStatus mram_store_check(MramStoreStatus *status);

BoardStatus mram_store_restore_redundant_copy(void);

BoardStatus mram_store_load_config(MramStoreConfig *config);

BoardStatus mram_store_save_config(const MramStoreConfig *config);

BoardStatus mram_store_load_service_data(MramStoreServiceData *service_data);

BoardStatus mram_store_save_service_data(const MramStoreServiceData *service_data);

BoardStatus mram_store_save_test_result(const MramStoreTestResult *test_result);

#endif // NATALIA_CORE_MRAM_STORE_H
