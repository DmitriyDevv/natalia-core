#include "board_api.h"

#include <stddef.h>
#include <string.h>

#include "dump_mode_config.h"
#include "test_mode_config.h"

#define BOARD_STUB_NAND_BANK_COUNT 2U
#define BOARD_STUB_NAND_PACKET_SIZE DUMP_MODE_PACKET_SIZE
#define BOARD_STUB_NAND_PACKET_COUNT 64U
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

static uint8_t board_stub_initialized;

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

    *is_powered = 1U;

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

BoardStatus board_rtc_get_time(InstrumentTime *time) {
    if (time == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    time->seconds = 0U;
    time->milliseconds = 0U;

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

BoardStatus board_read_power_status(uint32_t *power_status) {
    if (power_status == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    *power_status = 0U;

    return BOARD_OK;
}
