#include "board_api.h"

#include <stddef.h>
#include <string.h>

#include "board_comm_stub.h"
#include "board_stub.h"
#include "dump_mode_config.h"
#include "test_mode_config.h"

#define BOARD_STUB_NAND_BANK_COUNT 2U
#define BOARD_STUB_NAND_PACKET_SIZE DUMP_MODE_PACKET_SIZE
#define BOARD_STUB_NAND_PACKET_COUNT 256U
#define BOARD_STUB_NAND_SIZE ((size_t)BOARD_STUB_NAND_PACKET_SIZE * (size_t)BOARD_STUB_NAND_PACKET_COUNT)

#define BOARD_STUB_MRAM_COPY_COUNT 2U
#define BOARD_STUB_MRAM_COPY_SIZE 1024U

static uint8_t board_stub_nand[BOARD_STUB_NAND_BANK_COUNT][BOARD_STUB_NAND_SIZE];
static uint32_t board_stub_nand_committed_packets[BOARD_STUB_NAND_BANK_COUNT];
static uint32_t board_stub_nand_read_packet_count[BOARD_STUB_NAND_BANK_COUNT];
static uint32_t board_stub_nand_read_next_packet[BOARD_STUB_NAND_BANK_COUNT];
static uint8_t board_stub_nand_is_full[BOARD_STUB_NAND_BANK_COUNT];
static uint8_t board_stub_nand_is_powered[BOARD_STUB_NAND_BANK_COUNT];
static uint8_t board_stub_nand_is_connected[BOARD_STUB_NAND_BANK_COUNT];

static uint8_t board_stub_mram[BOARD_STUB_MRAM_COPY_COUNT][BOARD_STUB_MRAM_COPY_SIZE];
static uint8_t board_stub_mram_test_result[BOARD_STUB_MRAM_COPY_COUNT][2][BOARD_MRAM_TEST_RESULT_SIZE];

static uint8_t board_stub_initialized;

static bool board_stub_mram_write_fail;

static uint8_t board_stub_test_result_valid[2] = {1U, 1U};

static uint32_t board_stub_rtc_1hz_pending;
static uint32_t board_stub_ped_trigger_pending;

static int32_t board_stub_digital_temp_milli[2] = {25000, 25000};
static uint8_t board_stub_digital_temp_ready[2] = {1U, 1U};
static uint8_t board_stub_digital_temp_valid[2] = {1U, 1U};
static uint32_t board_stub_power_mv[2] = {3300U, 3300U};
static int32_t board_stub_power_ua[2] = {0, 0};
static uint8_t board_stub_power_ready[2] = {1U, 1U};
static uint8_t board_stub_ped_is_powered = 1U;

static uint32_t board_stub_rtc_seconds;
static uint16_t board_stub_rtc_milliseconds;

void board_stub_set_mram_write_fail(bool fail) {
    board_stub_mram_write_fail = fail;
}

void board_stub_set_rtc_time(uint32_t seconds, uint16_t milliseconds) {
    board_stub_rtc_seconds = seconds;
    board_stub_rtc_milliseconds = milliseconds;
}

void board_stub_set_rtc_1hz_events(uint32_t count) {
    board_stub_rtc_1hz_pending = count;
}

void board_stub_set_ped_trigger_events(uint32_t count) {
    board_stub_ped_trigger_pending = count;
}

void board_stub_set_digital_temp(BoardTempSensorId sensor, int32_t milli_c,
                                 bool valid) {
    uint8_t idx = (sensor == BOARD_TEMP_SENSOR_PED) ? 1U : 0U;
    board_stub_digital_temp_milli[idx] = milli_c;
    board_stub_digital_temp_ready[idx] = valid ? 1U : 0U;
    board_stub_digital_temp_valid[idx] = valid ? 1U : 0U;
}

void board_stub_set_power_monitor(BoardPowerMonitorId monitor, uint32_t mv,
                                  int32_t ua, bool ready) {
    uint8_t idx = (monitor == BOARD_POWER_MONITOR_PED) ? 1U : 0U;
    board_stub_power_mv[idx] = mv;
    board_stub_power_ua[idx] = ua;
    board_stub_power_ready[idx] = ready ? 1U : 0U;
}

void board_stub_set_ped_powered(bool powered) {
    board_stub_ped_is_powered = powered ? 1U : 0U;
}

void board_stub_set_test_result_valid(uint8_t nand_bank, bool valid) {
    if ((nand_bank == 1U) || (nand_bank == 2U)) {
        board_stub_test_result_valid[nand_bank - 1U] = valid ? 1U : 0U;
    }
}

static void board_stub_init_once(void) {
    if (board_stub_initialized == 0U) {
        (void)memset(board_stub_nand, 0xFF, sizeof(board_stub_nand));
        (void)memset(board_stub_mram, 0, sizeof(board_stub_mram));
        (void)memset(board_stub_nand_committed_packets, 0, sizeof(board_stub_nand_committed_packets));
        (void)memset(board_stub_nand_read_packet_count, 0, sizeof(board_stub_nand_read_packet_count));
        (void)memset(board_stub_nand_read_next_packet, 0, sizeof(board_stub_nand_read_next_packet));
        (void)memset(board_stub_nand_is_full, 0, sizeof(board_stub_nand_is_full));
        (void)memset(board_stub_nand_is_powered, 0, sizeof(board_stub_nand_is_powered));
        (void)memset(board_stub_nand_is_connected, 0, sizeof(board_stub_nand_is_connected));
        board_stub_initialized = 1U;
    }
}

static BoardStatus board_stub_get_nand_index(uint8_t bank_id, size_t *index) {
    if (index == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((bank_id == 0U) || (bank_id > BOARD_STUB_NAND_BANK_COUNT)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *index = (size_t)bank_id - 1U;

    return BOARD_OK;
}

static BoardStatus board_stub_check_nand_range(uint8_t bank_id,
                                               uint32_t address,
                                               size_t size,
                                               size_t *index) {
    size_t bank_index;
    size_t offset;
    BoardStatus status;

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    offset = (size_t)address;

    if (offset > BOARD_STUB_NAND_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > (BOARD_STUB_NAND_SIZE - offset)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (index != NULL) {
        *index = bank_index;
    }

    return BOARD_OK;
}

static BoardStatus board_stub_get_mram_index(uint8_t copy_id, size_t *index) {
    if (index == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((copy_id == 0U) || (copy_id > BOARD_STUB_MRAM_COPY_COUNT)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *index = (size_t)copy_id - 1U;

    return BOARD_OK;
}

static BoardStatus board_stub_check_mram_range(uint8_t copy_id,
                                               uint32_t offset,
                                               size_t size,
                                               size_t *index) {
    size_t copy_index;
    size_t local_offset;
    BoardStatus status;

    status = board_stub_get_mram_index(copy_id, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    local_offset = (size_t)offset;

    if (local_offset > BOARD_STUB_MRAM_COPY_SIZE) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > (BOARD_STUB_MRAM_COPY_SIZE - local_offset)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (index != NULL) {
        *index = copy_index;
    }

    return BOARD_OK;
}

static uint32_t board_stub_packet_count_from_end_offset(size_t end_offset) {
    size_t packets;

    if (end_offset == 0U) {
        return 0U;
    }

    packets = (end_offset + ((size_t)BOARD_STUB_NAND_PACKET_SIZE - 1U)) /
        (size_t)BOARD_STUB_NAND_PACKET_SIZE;

    if (packets > (size_t)BOARD_STUB_NAND_PACKET_COUNT) {
        packets = (size_t)BOARD_STUB_NAND_PACKET_COUNT;
    }

    return (uint32_t)packets;
}

BoardStatus board_init_hardware(void) {
    board_stub_init_once();
    return BOARD_OK;
}

BoardStatus board_enter_safe_config(void) {
    size_t index;

    board_stub_init_once();

    for (index = 0U; index < BOARD_STUB_NAND_BANK_COUNT; ++index) {
        board_stub_nand_is_powered[index] = 0U;
        board_stub_nand_is_connected[index] = 0U;
    }

    return BOARD_OK;
}

BoardStatus board_disconnect_signal_lines(BoardSignalTarget target) {
    (void)target;
    return BOARD_OK;
}

BoardStatus board_mram_read(uint8_t copy_id, uint32_t offset, void *buffer, size_t size) {
    size_t copy_index;
    BoardStatus status;

    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_check_mram_range(copy_id, offset, size, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        (void)memcpy(buffer, &board_stub_mram[copy_index][(size_t)offset], size);
    }

    return BOARD_OK;
}

BoardStatus board_mram_write(uint8_t copy_id, uint32_t offset, const void *buffer, size_t size) {
    size_t copy_index;
    BoardStatus status;

    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_check_mram_range(copy_id, offset, size, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (board_stub_mram_write_fail) {
        return BOARD_ERR_IO;
    }

    if (size > 0U) {
        (void)memcpy(&board_stub_mram[copy_index][(size_t)offset], buffer, size);
    }

    return BOARD_OK;
}

BoardStatus board_mram_check_crc(uint8_t copy_id, uint8_t *is_valid) {
    size_t copy_index;
    BoardStatus status;

    if (is_valid == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_mram_index(copy_id, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)copy_index;

    *is_valid = 1U;

    return BOARD_OK;
}

BoardStatus board_mram_restore_copy(uint8_t source_copy_id, uint8_t target_copy_id) {
    size_t source_index;
    size_t target_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_mram_index(source_copy_id, &source_index);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_stub_get_mram_index(target_copy_id, &target_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)memcpy(board_stub_mram[target_index],
                 board_stub_mram[source_index],
                 BOARD_STUB_MRAM_COPY_SIZE);

    return BOARD_OK;
}

BoardStatus board_mram_write_test_result(uint8_t copy_id,
                                         uint8_t nand_bank,
                                         const void *data,
                                         size_t size) {
    size_t copy_index;
    BoardStatus status;

    if ((data == NULL) || (size != BOARD_MRAM_TEST_RESULT_SIZE)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((nand_bank != 1U) && (nand_bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_mram_index(copy_id, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)memcpy(board_stub_mram_test_result[copy_index][nand_bank - 1U],
                 data, size);

    return BOARD_OK;
}

BoardStatus board_mram_read_test_result(uint8_t copy_id,
                                        uint8_t nand_bank,
                                        void *data,
                                        size_t size,
                                        uint8_t *is_valid,
                                        uint8_t *crc_out) {
    size_t copy_index;
    BoardStatus status;

    if ((data == NULL) || (size != BOARD_MRAM_TEST_RESULT_SIZE) || (is_valid == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if ((nand_bank != 1U) && (nand_bank != 2U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_mram_index(copy_id, &copy_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)memcpy(data,
                 board_stub_mram_test_result[copy_index][nand_bank - 1U],
                 size);
    *is_valid = board_stub_test_result_valid[nand_bank - 1U];

    if (crc_out != NULL) {
        const uint8_t *bytes = (const uint8_t *)data;
        uint16_t checksum = 0U;
        size_t index;

        for (index = 0U; index < size; ++index) {
            checksum = (uint16_t)(checksum + bytes[index]);
        }

        crc_out[0] = (uint8_t)(checksum & 0xFFU);
        crc_out[1] = (uint8_t)((checksum >> 8) & 0xFFU);
    }

    return BOARD_OK;
}

BoardStatus board_nand_power_on(uint8_t bank_id) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    board_stub_nand_is_powered[bank_index] = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_power_off(uint8_t bank_id) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    board_stub_nand_is_powered[bank_index] = 0U;
    board_stub_nand_is_connected[bank_index] = 0U;

    return BOARD_OK;
}

BoardStatus board_nand_is_powered(uint8_t bank_id, uint8_t *is_powered) {
    size_t bank_index;
    BoardStatus status;

    if (is_powered == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    *is_powered = board_stub_nand_is_powered[bank_index];

    return BOARD_OK;
}

BoardStatus board_nand_connect(uint8_t bank_id) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (board_stub_nand_is_powered[bank_index] == 0U) {
        return BOARD_ERR_NOT_READY;
    }

    board_stub_nand_is_connected[bank_index] = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_disconnect(uint8_t bank_id) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    board_stub_nand_is_connected[bank_index] = 0U;

    return BOARD_OK;
}

BoardStatus board_nand_read(uint8_t bank_id, uint32_t address, void *buffer, size_t size) {
    size_t bank_index;
    BoardStatus status;

    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_check_nand_range(bank_id, address, size, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        (void)memcpy(buffer, &board_stub_nand[bank_index][(size_t)address], size);
    }

    return BOARD_OK;
}

BoardStatus board_nand_write(uint8_t bank_id, uint32_t address, const void *buffer, size_t size) {
    size_t bank_index;
    size_t end_offset;
    uint32_t committed_packets;
    BoardStatus status;

    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_check_nand_range(bank_id, address, size, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (size > 0U) {
        (void)memcpy(&board_stub_nand[bank_index][(size_t)address], buffer, size);
    }

    end_offset = (size_t)address + size;
    committed_packets = board_stub_packet_count_from_end_offset(end_offset);

    if (committed_packets > board_stub_nand_committed_packets[bank_index]) {
        board_stub_nand_committed_packets[bank_index] = committed_packets;
    }

    if (board_stub_nand_committed_packets[bank_index] >= BOARD_STUB_NAND_PACKET_COUNT) {
        board_stub_nand_is_full[bank_index] = 1U;
    }

    return BOARD_OK;
}

BoardStatus board_nand_open_write(uint8_t bank_id, uint32_t start_packet_count) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (start_packet_count > BOARD_STUB_NAND_PACKET_COUNT) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_nand_committed_packets[bank_index] = start_packet_count;
    board_stub_nand_is_full[bank_index] =
        (start_packet_count >= BOARD_STUB_NAND_PACKET_COUNT) ? 1U : 0U;

    return BOARD_OK;
}

BoardStatus board_nand_write_packet(uint8_t bank_id, const void *packet) {
    size_t bank_index;
    size_t offset;
    BoardStatus status;

    if (packet == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (board_stub_nand_committed_packets[bank_index] >= BOARD_STUB_NAND_PACKET_COUNT) {
        board_stub_nand_is_full[bank_index] = 1U;
        return BOARD_ERR_IO;
    }

    offset = (size_t)board_stub_nand_committed_packets[bank_index] *
        (size_t)BOARD_STUB_NAND_PACKET_SIZE;

    (void)memcpy(&board_stub_nand[bank_index][offset],
                 packet,
                 BOARD_STUB_NAND_PACKET_SIZE);

    ++board_stub_nand_committed_packets[bank_index];

    if (board_stub_nand_committed_packets[bank_index] >= BOARD_STUB_NAND_PACKET_COUNT) {
        board_stub_nand_is_full[bank_index] = 1U;
    }

    return BOARD_OK;
}

BoardStatus board_nand_write_poll(uint8_t bank_id, uint8_t *is_idle) {
    size_t bank_index;
    BoardStatus status;

    if (is_idle == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)bank_index;

    *is_idle = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_write_flush(uint8_t bank_id, uint8_t *is_done) {
    size_t bank_index;
    BoardStatus status;

    if (is_done == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)bank_index;

    *is_done = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_open_read(uint8_t bank_id, uint32_t packet_count) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (packet_count > BOARD_STUB_NAND_PACKET_COUNT) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_nand_read_packet_count[bank_index] = packet_count;
    board_stub_nand_read_next_packet[bank_index] = 0U;

    return BOARD_OK;
}

BoardStatus board_nand_read_packet(uint8_t bank_id, uint32_t packet_index, void *packet) {
    size_t bank_index;
    size_t offset;
    BoardStatus status;

    if (packet == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (packet_index >= BOARD_STUB_NAND_PACKET_COUNT) {
        return BOARD_ERR_INVALID_ARG;
    }

    offset = (size_t)packet_index * (size_t)BOARD_STUB_NAND_PACKET_SIZE;

    (void)memcpy(packet,
                 &board_stub_nand[bank_index][offset],
                 BOARD_STUB_NAND_PACKET_SIZE);

    return BOARD_OK;
}

BoardStatus board_nand_read_next_packet(uint8_t bank_id, void *packet, uint8_t *has_packet) {
    size_t bank_index;
    uint32_t packet_index;
    BoardStatus status;

    if ((packet == NULL) || (has_packet == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    if (board_stub_nand_read_next_packet[bank_index] >=
        board_stub_nand_read_packet_count[bank_index]) {
        *has_packet = 0U;
        return BOARD_OK;
    }

    packet_index = board_stub_nand_read_next_packet[bank_index];

    status = board_nand_read_packet(bank_id, packet_index, packet);
    if (status != BOARD_OK) {
        return status;
    }

    ++board_stub_nand_read_next_packet[bank_index];
    *has_packet = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_get_capacity_packets(uint8_t bank_id, uint32_t *packet_capacity) {
    size_t bank_index;
    BoardStatus status;

    if (packet_capacity == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)bank_index;

    *packet_capacity = BOARD_STUB_NAND_PACKET_COUNT;

    return BOARD_OK;
}

BoardStatus board_nand_get_committed_packet_count(uint8_t bank_id, uint32_t *packet_count) {
    size_t bank_index;
    BoardStatus status;

    if (packet_count == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    *packet_count = board_stub_nand_committed_packets[bank_index];

    return BOARD_OK;
}

BoardStatus board_nand_erase_start(uint8_t bank_id) {
    size_t bank_index;
    BoardStatus status;

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)memset(board_stub_nand[bank_index], 0xFF, BOARD_STUB_NAND_SIZE);
    board_stub_nand_committed_packets[bank_index] = 0U;
    board_stub_nand_read_packet_count[bank_index] = 0U;
    board_stub_nand_read_next_packet[bank_index] = 0U;
    board_stub_nand_is_full[bank_index] = 0U;

    return BOARD_OK;
}

BoardStatus board_nand_erase_is_done(uint8_t bank_id, uint8_t *is_done) {
    size_t bank_index;
    BoardStatus status;

    if (is_done == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    (void)bank_index;

    *is_done = 1U;

    return BOARD_OK;
}

BoardStatus board_nand_is_full(uint8_t bank_id, uint8_t *is_full) {
    size_t bank_index;
    BoardStatus status;

    if (is_full == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    board_stub_init_once();

    status = board_stub_get_nand_index(bank_id, &bank_index);
    if (status != BOARD_OK) {
        return status;
    }

    *is_full = board_stub_nand_is_full[bank_index];

    return BOARD_OK;
}

BoardStatus board_ped_power_on(void) {
    return BOARD_OK;
}

BoardStatus board_ped_power_off(void) {
    return BOARD_OK;
}

BoardStatus board_ped_is_powered(uint8_t *is_powered) {
    if (is_powered == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_powered = board_stub_ped_is_powered;

    return BOARD_OK;
}

BoardStatus board_ped_reg_init(void) {
    return BOARD_OK;
}

BoardStatus board_ped_read_status(uint32_t *status) {
    if (status == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *status = 0U;

    return BOARD_OK;
}

BoardStatus board_ped_write_config(const void *config, size_t size) {
    if ((config == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

BoardStatus board_ped_read_event(void *event_buffer, size_t buffer_size, size_t *bytes_read) {
    if ((event_buffer == NULL) && (buffer_size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_read == NULL) {
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

BoardStatus board_ped_take_trigger_events(uint32_t *event_count) {
    if (event_count == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = board_stub_ped_trigger_pending;
    board_stub_ped_trigger_pending = 0U;

    return BOARD_OK;
}

BoardStatus board_rtc_get_time(InstrumentTime *time) {
    if (time == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    time->seconds = board_stub_rtc_seconds;
    time->milliseconds = board_stub_rtc_milliseconds;

    return BOARD_OK;
}

BoardStatus board_rtc_set_time(const InstrumentTime *time) {
    if (time == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (time->milliseconds >= 1000U) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

BoardStatus board_usb_write(const void *buffer, size_t size, size_t *bytes_written) {
    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_written == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = size;

    return BOARD_OK;
}

BoardStatus board_usb_is_ready(uint8_t *is_ready) {
    if (is_ready == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 1U;

    return BOARD_OK;
}

BoardStatus board_data_write(const void *buffer, size_t size, size_t *bytes_written) {
    if ((buffer == NULL) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (bytes_written == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = size;

    return BOARD_OK;
}

BoardStatus board_data_is_ready(uint8_t *is_ready) {
    if (is_ready == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_ready = 1U;

    return BOARD_OK;
}

BoardStatus board_read_power_status(uint32_t *power_status) {
    if (power_status == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *power_status = 0U;

    return BOARD_OK;
}

BoardStatus board_rtc_take_1hz_events(uint32_t *event_count) {
    if (event_count == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *event_count = board_stub_rtc_1hz_pending;
    board_stub_rtc_1hz_pending = 0U;

    return BOARD_OK;
}

BoardStatus board_temp_init(void) {
    return BOARD_OK;
}

BoardStatus board_temp_start(void) {
    return BOARD_OK;
}

BoardStatus board_temp_stop(void) {
    return BOARD_OK;
}

BoardStatus board_read_temp(BoardTempSample *sample) {
    if (sample == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    sample->temperature_milli_c = 25000;
    sample->ready = 1U;
    sample->range_valid = 1U;

    return BOARD_OK;
}

BoardStatus board_read_temp_milli_c(int32_t *temperature_milli_c) {
    if (temperature_milli_c == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *temperature_milli_c = 25000;

    return BOARD_OK;
}

BoardStatus board_temp_digital_init(void) {
    return BOARD_OK;
}

BoardStatus board_read_digital_temp(BoardTempSensorId sensor, BoardDigitalTempSample *sample) {
    uint8_t idx = (sensor == BOARD_TEMP_SENSOR_PED) ? 1U : 0U;

    if (sample == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    sample->temperature_milli_c = board_stub_digital_temp_milli[idx];
    sample->ready = board_stub_digital_temp_ready[idx];
    sample->range_valid = board_stub_digital_temp_valid[idx];

    return BOARD_OK;
}

BoardStatus board_read_digital_temp_milli_c(BoardTempSensorId sensor, int32_t *temperature_milli_c) {
    (void)sensor;

    if (temperature_milli_c == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *temperature_milli_c = 25000;

    return BOARD_OK;
}

BoardStatus board_power_monitor_init(void) {
    return BOARD_OK;
}

BoardStatus board_read_power_monitor(BoardPowerMonitorId monitor, BoardPowerSample *sample) {
    uint8_t idx = (monitor == BOARD_POWER_MONITOR_PED) ? 1U : 0U;

    if (sample == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    memset(sample, 0, sizeof(*sample));

    sample->bus_voltage_mv = board_stub_power_mv[idx];
    sample->current_ua = board_stub_power_ua[idx];
    sample->ready = board_stub_power_ready[idx];
    sample->conversion_ready = board_stub_power_ready[idx];

    return BOARD_OK;
}

#if defined(NATALIA_ENABLE_BOARD_TEST_HOOKS) && (NATALIA_ENABLE_BOARD_TEST_HOOKS != 0)
BoardStatus board_usb_test_capture_start(uint32_t packet_count, uint32_t acquisition_period_ticks) {
    (void)packet_count;
    (void)acquisition_period_ticks;

    return BOARD_OK;
}

BoardStatus board_usb_test_capture_get_result(uint32_t *bytes_written,
                                              uint32_t *expected_bytes,
                                              uint32_t *error_count) {
    if ((bytes_written == NULL) || (expected_bytes == NULL) || (error_count == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    *bytes_written = 0U;
    *expected_bytes = 0U;
    *error_count = 0U;

    return BOARD_OK;
}
#endif

static uint8_t board_comm_stub_rx_data[BOARD_COMM_MAX_MESSAGE_DATA];
static uint16_t board_comm_stub_rx_length;
static uint16_t board_comm_stub_rx_message_id;
static uint16_t board_comm_stub_rx_address_from;
static uint16_t board_comm_stub_rx_address_to;
static bool board_comm_stub_rx_pending;

static uint8_t board_comm_stub_tx_data[BOARD_COMM_MAX_MESSAGE_DATA];
static uint16_t board_comm_stub_tx_length;
static uint16_t board_comm_stub_tx_message_id;
static uint16_t board_comm_stub_tx_address_to;
static uint32_t board_comm_stub_tx_total;

void board_comm_stub_reset(void) {
    board_comm_stub_rx_length = 0U;
    board_comm_stub_rx_message_id = 0U;
    board_comm_stub_rx_address_from = 0U;
    board_comm_stub_rx_address_to = 0U;
    board_comm_stub_rx_pending = false;

    board_comm_stub_tx_length = 0U;
    board_comm_stub_tx_message_id = 0U;
    board_comm_stub_tx_address_to = 0U;
    board_comm_stub_tx_total = 0U;
}

void board_comm_stub_inject_rx(uint16_t message_id,
                               uint16_t address_from,
                               uint16_t address_to,
                               const uint8_t* data,
                               uint16_t length) {
    if (length > (uint16_t)BOARD_COMM_MAX_MESSAGE_DATA) {
        length = (uint16_t)BOARD_COMM_MAX_MESSAGE_DATA;
    }

    if ((data != NULL) && (length > 0U)) {
        (void)memcpy(board_comm_stub_rx_data, data, length);
    }

    board_comm_stub_rx_message_id = message_id;
    board_comm_stub_rx_address_from = address_from;
    board_comm_stub_rx_address_to = address_to;
    board_comm_stub_rx_length = length;
    board_comm_stub_rx_pending = true;
}

uint32_t board_comm_stub_tx_count(void) {
    return board_comm_stub_tx_total;
}

bool board_comm_stub_last_tx(uint16_t* message_id,
                             uint16_t* address_to,
                             uint8_t* buffer,
                             uint16_t capacity,
                             uint16_t* length) {
    if (board_comm_stub_tx_total == 0U) {
        return false;
    }

    if (message_id != NULL) {
        *message_id = board_comm_stub_tx_message_id;
    }

    if (address_to != NULL) {
        *address_to = board_comm_stub_tx_address_to;
    }

    if (length != NULL) {
        *length = board_comm_stub_tx_length;
    }

    if ((buffer != NULL) && (capacity >= board_comm_stub_tx_length)) {
        (void)memcpy(buffer, board_comm_stub_tx_data, board_comm_stub_tx_length);
    }

    return true;
}

BoardStatus board_comm_init(void) {
    board_comm_stub_reset();
    return BOARD_OK;
}

void board_comm_close(void) {
}

void board_comm_poll(uint32_t now_ms) {
    (void)now_ms;
}

BoardStatus board_comm_send(const BoardCommMessage* message) {
    uint16_t length;

    if (message == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    length = message->length;
    if (length > (uint16_t)BOARD_COMM_MAX_MESSAGE_DATA) {
        length = (uint16_t)BOARD_COMM_MAX_MESSAGE_DATA;
    }

    if ((message->data != NULL) && (length > 0U)) {
        (void)memcpy(board_comm_stub_tx_data, message->data, length);
    }

    board_comm_stub_tx_message_id = message->message_id;
    board_comm_stub_tx_address_to = message->address_to;
    board_comm_stub_tx_length = length;
    ++board_comm_stub_tx_total;

    return BOARD_OK;
}

BoardStatus board_comm_receive(BoardCommMessage* message, uint8_t* buffer, uint16_t capacity) {
    if ((message == NULL) || (buffer == NULL)) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (!board_comm_stub_rx_pending) {
        return BOARD_ERR_NOT_READY;
    }

    if (capacity < board_comm_stub_rx_length) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (board_comm_stub_rx_length > 0U) {
        (void)memcpy(buffer, board_comm_stub_rx_data, board_comm_stub_rx_length);
    }

    message->message_id = board_comm_stub_rx_message_id;
    message->address_from = board_comm_stub_rx_address_from;
    message->address_to = board_comm_stub_rx_address_to;
    message->length = board_comm_stub_rx_length;
    message->data = buffer;

    board_comm_stub_rx_pending = false;

    return BOARD_OK;
}

void board_comm_get_status(BoardCommStatus* status) {
    if (status == NULL) {
        return;
    }

    status->is_online = true;
    status->tx_busy = false;
    status->tx_messages_ok = board_comm_stub_tx_total;
    status->tx_messages_failed = 0U;
}
