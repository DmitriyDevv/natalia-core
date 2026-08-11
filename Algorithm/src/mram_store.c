#include "mram_store.h"

#define MRAM_COPY_1 1U
#define MRAM_COPY_2 2U

#define MRAM_CONFIG_OFFSET 0x0000U
#define MRAM_SERVICE_DATA_OFFSET 0x0100U

_Static_assert(TEST_MODE_NERR_BYTE_SIZE == BOARD_MRAM_TEST_RESULT_SIZE,
               "Nerr packed size must match the Board_API test-result region");

static uint8_t mram_store_nerr_buffer[TEST_MODE_NERR_BYTE_SIZE];

static void pack_nerr(const uint32_t *nerr, uint8_t *out) {
    uint32_t i;

    for (i = 0U; i < TEST_MODE_BLOCK_COUNT; ++i) {
        uint32_t value = (nerr[i] > TEST_MODE_NERR_MAX) ? TEST_MODE_NERR_MAX : nerr[i];
        out[(i * 3U) + 0U] = (uint8_t)(value & 0xFFU);
        out[(i * 3U) + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
        out[(i * 3U) + 2U] = (uint8_t)((value >> 16U) & 0xFFU);
    }
}

static void unpack_nerr(const uint8_t *in, uint32_t *nerr) {
    uint32_t i;

    for (i = 0U; i < TEST_MODE_BLOCK_COUNT; ++i) {
        nerr[i] = (uint32_t)in[(i * 3U) + 0U] |
                  ((uint32_t)in[(i * 3U) + 1U] << 8U) |
                  ((uint32_t)in[(i * 3U) + 2U] << 16U);
    }
}

static BoardStatus get_valid_copy(uint8_t *copy_id) {
    MramStoreStatus status = {0};
    BoardStatus board_status;

    if (copy_id == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_status = mram_store_check(&status);
    if (board_status != BOARD_OK) {
        return board_status;
    }

    if (status.copy1_valid != 0U) {
        *copy_id = MRAM_COPY_1;
        return BOARD_OK;
    }

    if (status.copy2_valid != 0U) {
        *copy_id = MRAM_COPY_2;
        return BOARD_OK;
    }

    return BOARD_ERR_CRC;
}

static BoardStatus wait_write_interval(void) {
    return BOARD_OK;
}

static BoardStatus check_written_copy(uint8_t copy_id) {
    uint8_t is_valid = 0U;
    BoardStatus status = board_mram_check_crc(copy_id, &is_valid);

    if (status != BOARD_OK) {
        return status;
    }

    return (is_valid != 0U) ? BOARD_OK : BOARD_ERR_CRC;
}

static BoardStatus write_both_copies(uint32_t offset, const void *buffer, size_t size) {
    BoardStatus status;

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_mram_write(MRAM_COPY_1, offset, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = wait_write_interval();
    if (status != BOARD_OK) {
        return status;
    }

    status = board_mram_write(MRAM_COPY_2, offset, buffer, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = check_written_copy(MRAM_COPY_1);
    if (status != BOARD_OK) {
        return status;
    }

    return check_written_copy(MRAM_COPY_2);
}

BoardStatus mram_store_check(MramStoreStatus *status) {
    BoardStatus board_status;

    if (status == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status->copy1_valid = 0U;
    status->copy2_valid = 0U;

    board_status = board_mram_check_crc(MRAM_COPY_1, &status->copy1_valid);
    if (board_status != BOARD_OK) {
        return board_status;
    }

    return board_mram_check_crc(MRAM_COPY_2, &status->copy2_valid);
}

BoardStatus mram_store_restore_redundant_copy(void) {
    MramStoreStatus status = {0};
    BoardStatus board_status = mram_store_check(&status);

    if (board_status != BOARD_OK) {
        return board_status;
    }

    if ((status.copy1_valid != 0U) && (status.copy2_valid == 0U)) {
        return board_mram_restore_copy(MRAM_COPY_1, MRAM_COPY_2);
    }

    if ((status.copy1_valid == 0U) && (status.copy2_valid != 0U)) {
        return board_mram_restore_copy(MRAM_COPY_2, MRAM_COPY_1);
    }

    if ((status.copy1_valid == 0U) && (status.copy2_valid == 0U)) {
        return BOARD_ERR_CRC;
    }

    return BOARD_OK;
}

BoardStatus mram_store_load_config(MramStoreConfig *config) {
    uint8_t copy_id = 0U;
    BoardStatus status;

    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = mram_store_restore_redundant_copy();
    if (status != BOARD_OK) {
        return status;
    }

    status = get_valid_copy(&copy_id);
    if (status != BOARD_OK) {
        return status;
    }

    return board_mram_read(copy_id, MRAM_CONFIG_OFFSET, config, sizeof(*config));
}

BoardStatus mram_store_save_config(const MramStoreConfig *config) {
    if (config == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    return write_both_copies(MRAM_CONFIG_OFFSET, config, sizeof(*config));
}

BoardStatus mram_store_save_addresses(uint16_t device_id, uint16_t destination_id) {
    MramStoreConfig config = {0};
    BoardStatus status;

    status = mram_store_load_config(&config);
    if (status != BOARD_OK) {
        return status;
    }

    config.device_id = device_id;
    config.destination_id = destination_id;

    return mram_store_save_config(&config);
}

BoardStatus mram_store_load_service_data(MramStoreServiceData *service_data) {
    uint8_t copy_id = 0U;
    BoardStatus status;

    if (service_data == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = mram_store_restore_redundant_copy();
    if (status != BOARD_OK) {
        return status;
    }

    status = get_valid_copy(&copy_id);
    if (status != BOARD_OK) {
        return status;
    }

    return board_mram_read(copy_id, MRAM_SERVICE_DATA_OFFSET, service_data, sizeof(*service_data));
}

BoardStatus mram_store_save_service_data(const MramStoreServiceData *service_data) {
    if (service_data == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    return write_both_copies(MRAM_SERVICE_DATA_OFFSET, service_data, sizeof(*service_data));
}

BoardStatus mram_store_save_test_result(const MramStoreTestResult *test_result) {
    BoardStatus status;

    if (test_result == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((test_result->bank != 1U) && (test_result->bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    pack_nerr(test_result->nerr, mram_store_nerr_buffer);

    status = board_mram_write_test_result(MRAM_COPY_1, test_result->bank,
                                          mram_store_nerr_buffer,
                                          TEST_MODE_NERR_BYTE_SIZE);
    if (status != BOARD_OK) {
        return status;
    }

    status = wait_write_interval();
    if (status != BOARD_OK) {
        return status;
    }

    return board_mram_write_test_result(MRAM_COPY_2, test_result->bank,
                                        mram_store_nerr_buffer,
                                        TEST_MODE_NERR_BYTE_SIZE);
}

BoardStatus mram_store_load_test_result(uint8_t bank, MramStoreTestResult *test_result) {
    uint8_t is_valid = 0U;
    BoardStatus status;

    if (test_result == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((bank != 1U) && (bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = board_mram_read_test_result(MRAM_COPY_1, bank,
                                         mram_store_nerr_buffer,
                                         TEST_MODE_NERR_BYTE_SIZE, &is_valid, NULL);
    if ((status == BOARD_OK) && (is_valid == 0U)) {
        status = board_mram_read_test_result(MRAM_COPY_2, bank,
                                             mram_store_nerr_buffer,
                                             TEST_MODE_NERR_BYTE_SIZE, &is_valid, NULL);
    }

    if (status != BOARD_OK) {
        return status;
    }

    if (is_valid == 0U) {
        return BOARD_ERR_CRC;
    }

    unpack_nerr(mram_store_nerr_buffer, test_result->nerr);
    test_result->bank = bank;

    return BOARD_OK;
}
