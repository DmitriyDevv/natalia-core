#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../BSP/Board_API/include/board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "nand_mt29f.h"
#include "nand_storage.h"
#include "qspi.h"
#include "status.h"
#include "timebase.h"

#define TEST_BANK_ID 2U
#define TEST_BANK_ENUM NAND_MT29F_BANK_2

#define TEST_PACKET_COUNT 16U
#define TEST_ERASE_FIRST_BLOCK 0U
#define TEST_ERASE_BLOCK_COUNT 8U

#define TEST_TIMEOUT_LOOPS 8000000UL
#define TEST_MAX_MISMATCH_PRINT 16U

static uint8_t packet_tx[TEST_PACKET_COUNT][NAND_STORAGE_PACKET_SIZE] __attribute__((aligned(4)));
static uint8_t packet_rx[NAND_STORAGE_PACKET_SIZE] __attribute__((aligned(4)));

static void halt(void) {
    while (1) {}
}

static void halt_on_error(const char* text, BoardStatus status) {
    debug_log_write_u32(text, (uint32_t)status);
    halt();
}

static uint32_t xorshift32(uint32_t value) {
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;

    if (value == 0U) {
        value = 0x6D2B79F5UL;
    }

    return value;
}

static uint32_t make_seed(uint32_t packet_index) {
    uint32_t seed;

    seed = 0xA341316CUL;
    seed ^= packet_index * 0x9E3779B9UL;
    seed ^= 0x85EBCA6BUL;
    seed = xorshift32(seed);

    return seed;
}

static void fill_packet(uint8_t* buffer,
                        size_t size,
                        uint32_t packet_index) {
    size_t i;
    uint32_t state;
    uint32_t mixed;

    state = make_seed(packet_index);

    for (i = 0U; i < size; ++i) {
        state = xorshift32(state + (uint32_t)i + 0x7F4A7C15UL);
        mixed = state ^ (state >> 11U) ^ ((uint32_t)i * 0x45D9F3BUL);
        buffer[i] = (uint8_t)(mixed & 0xFFU);
    }
}

static void clear_packet(uint8_t* buffer, size_t size) {
    size_t i;

    for (i = 0U; i < size; ++i) {
        buffer[i] = 0U;
    }
}

static uint32_t hash_buffer(const uint8_t* buffer, size_t size) {
    size_t i;
    uint32_t hash;

    hash = 2166136261UL;

    for (i = 0U; i < size; ++i) {
        hash ^= buffer[i];
        hash *= 16777619UL;
    }

    return hash;
}

static uint32_t compare_packets(const uint8_t* expected,
                                const uint8_t* actual,
                                size_t size,
                                uint32_t packet_index) {
    size_t i;
    uint32_t errors;
    uint32_t printed;

    errors = 0U;
    printed = 0U;

    for (i = 0U; i < size; ++i) {
        if (expected[i] != actual[i]) {
            if (printed < TEST_MAX_MISMATCH_PRINT) {
                debug_log_write_u32("packet mismatch index = ", packet_index);
                debug_log_write_u32("offset = ", (uint32_t)i);
                debug_log_write_u32("expected = ", (uint32_t)expected[i]);
                debug_log_write_u32("actual = ", (uint32_t)actual[i]);
                ++printed;
            }

            ++errors;
        }
    }

    return errors;
}

static void print_qspi_snapshot(const char* label) {
    QspiDebugSnapshot snapshot;
    BoardStatus status;

    status = qspi_debug_snapshot(&snapshot);
    if (status != BOARD_OK) {
        debug_log_write_u32("qspi_debug_snapshot failed = ", (uint32_t)status);
        return;
    }

    debug_log_write_u32(label, 0U);
    debug_log_write_u32("qspi_cr = ", snapshot.qspi_cr);
    debug_log_write_u32("qspi_dcr = ", snapshot.qspi_dcr);
    debug_log_write_u32("qspi_sr = ", snapshot.qspi_sr);
    debug_log_write_u32("qspi_fcr = ", snapshot.qspi_fcr);
    debug_log_write_u32("qspi_dlr = ", snapshot.qspi_dlr);
    debug_log_write_u32("qspi_ccr = ", snapshot.qspi_ccr);
    debug_log_write_u32("qspi_ar = ", snapshot.qspi_ar);
    debug_log_write_u32("qspi_abr = ", snapshot.qspi_abr);
    debug_log_write_u32("dma_isr = ", snapshot.dma_isr);
    debug_log_write_u32("dma_ccr = ", snapshot.dma_ccr);
    debug_log_write_u32("dma_cndtr = ", snapshot.dma_cndtr);
    debug_log_write_u32("dma_cpar = ", snapshot.dma_cpar);
    debug_log_write_u32("dma_cmar = ", snapshot.dma_cmar);
    debug_log_write_u32("dma_cselr = ", snapshot.dma_cselr);
    debug_log_write_u32("dma_state = ", snapshot.dma_state);
    debug_log_write_u32("dma_status = ", snapshot.dma_status);
    debug_log_write_u32("poll_state = ", snapshot.poll_state);
    debug_log_write_u32("poll_status = ", snapshot.poll_status);
}

static void print_storage_info(const char* label) {
    NandStorageInfo info;
    BoardStatus status;

    status = nand_storage_get_info(&info);
    if (status != BOARD_OK) {
        debug_log_write_u32("nand_storage_get_info failed = ", (uint32_t)status);
        return;
    }

    debug_log_write_u32(label, 0U);
    debug_log_write_u32("mode = ", (uint32_t)info.mode);
    debug_log_write_u32("bank = ", (uint32_t)info.bank);
    debug_log_write_u32("mounted = ", (uint32_t)info.mounted);
    debug_log_write_u32("is_full = ", (uint32_t)info.is_full);
    debug_log_write_u32("write_active = ", (uint32_t)info.write_active);
    debug_log_write_u32("write_queued = ", (uint32_t)info.write_queued);
    debug_log_write_u32("committed_packet_count = ", info.committed_packet_count);
    debug_log_write_u32("next_packet_index = ", info.next_packet_index);
    debug_log_write_u32("next_block = ", info.next_block);
    debug_log_write_u32("next_page = ", info.next_page);
    debug_log_write_u32("read_packet_count = ", info.read_packet_count);
    debug_log_write_u32("read_next_packet_index = ", info.read_next_packet_index);
    debug_log_write_u32("erase_start_block = ", info.erase_start_block);
    debug_log_write_u32("erase_next_block = ", info.erase_next_block);
    debug_log_write_u32("erase_end_block = ", info.erase_end_block);
    debug_log_write_u32("erase_erased_blocks = ", info.erase_erased_blocks);
    debug_log_write_u32("erase_skipped_bad_blocks = ", info.erase_skipped_bad_blocks);
    debug_log_write_u32("erase_failed_blocks = ", info.erase_failed_blocks);
}

static BoardStatus wait_storage_idle(void) {
    BoardStatus status;
    uint8_t is_idle;
    uint32_t timeout;

    is_idle = 0U;
    timeout = TEST_TIMEOUT_LOOPS;

    while (is_idle == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        status = nand_storage_write_flush(&is_idle);
        if (status != BOARD_OK) {
            return status;
        }

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus write_packet_wait(const void* packet) {
    BoardStatus status;
    uint32_t timeout;

    timeout = TEST_TIMEOUT_LOOPS;

    while (timeout > 0U) {
        status = nand_storage_write_packet(packet);
        if (status == BOARD_OK) {
            return BOARD_OK;
        }

        if (status != BOARD_ERR_BUSY) {
            return status;
        }

        status = nand_storage_write_poll(0);
        if (status != BOARD_OK) {
            return status;
        }

        --timeout;
    }

    return BOARD_ERR_TIMEOUT;
}

static BoardStatus wait_erase_done(void) {
    BoardStatus status;
    uint8_t is_done;
    uint32_t timeout;

    is_done = 0U;
    timeout = TEST_TIMEOUT_LOOPS;

    while (is_done == 0U) {
        if (timeout == 0U) {
            return BOARD_ERR_TIMEOUT;
        }

        status = nand_storage_erase_bank_poll(&is_done);
        if (status != BOARD_OK) {
            return status;
        }

        print_storage_info("storage erase progress");

        --timeout;
    }

    return BOARD_OK;
}

static BoardStatus prepare_test_packets(void) {
    uint32_t index;

    for (index = 0U; index < TEST_PACKET_COUNT; ++index) {
        fill_packet(packet_tx[index],
                    NAND_STORAGE_PACKET_SIZE,
                    index);
        debug_log_write_u32("prepared packet index = ", index);
        debug_log_write_u32("prepared packet hash = ",
                            hash_buffer(packet_tx[index],
                                        NAND_STORAGE_PACKET_SIZE));
    }

    return BOARD_OK;
}

static BoardStatus run_storage_erase_range_test(void) {
    BoardStatus status;

    debug_log_write_u32("=== STORAGE ERASE RANGE TEST START ===", 0U);

    status = nand_storage_erase_range_start(TEST_BANK_ENUM,
                                            TEST_ERASE_FIRST_BLOCK,
                                            TEST_ERASE_BLOCK_COUNT);
    if (status != BOARD_OK) {
        print_storage_info("storage after erase range start fail");
        print_qspi_snapshot("qspi after erase range start fail");
        return status;
    }

    print_storage_info("storage after erase range start");

    status = wait_erase_done();
    if (status != BOARD_OK) {
        print_storage_info("storage after erase range poll fail");
        print_qspi_snapshot("qspi after erase range poll fail");
        return status;
    }

    print_storage_info("storage after erase range done");

    debug_log_write_u32("=== STORAGE ERASE RANGE TEST OK ===", 0U);

    return BOARD_OK;
}

static BoardStatus run_storage_write_test(void) {
    BoardStatus status;
    uint32_t index;

    debug_log_write_u32("=== STORAGE WRITE TEST START ===", 0U);

    status = nand_storage_open_write(TEST_BANK_ENUM, 0U);
    if (status != BOARD_OK) {
        return status;
    }

    print_storage_info("storage after open_write");

    for (index = 0U; index < TEST_PACKET_COUNT; ++index) {
        debug_log_write_u32("write packet index = ", index);

        status = write_packet_wait(packet_tx[index]);
        if (status != BOARD_OK) {
            print_storage_info("storage after write packet fail");
            print_qspi_snapshot("qspi after write packet fail");
            return status;
        }

        print_storage_info("storage after packet accepted");
    }

    status = wait_storage_idle();
    if (status != BOARD_OK) {
        print_storage_info("storage after wait idle fail");
        print_qspi_snapshot("qspi after wait idle fail");
        return status;
    }

    print_storage_info("storage after write flush");

    debug_log_write_u32("=== STORAGE WRITE TEST OK ===", 0U);

    return BOARD_OK;
}

static BoardStatus run_storage_read_by_index_test(void) {
    BoardStatus status;
    uint32_t index;
    uint32_t errors;
    uint32_t total_errors;

    debug_log_write_u32("=== STORAGE READ BY INDEX TEST START ===", 0U);

    status = nand_storage_open_read(TEST_BANK_ENUM, TEST_PACKET_COUNT);
    if (status != BOARD_OK) {
        return status;
    }

    total_errors = 0U;

    for (index = 0U; index < TEST_PACKET_COUNT; ++index) {
        clear_packet(packet_rx, NAND_STORAGE_PACKET_SIZE);

        status = nand_storage_read_packet(index, packet_rx);
        if (status != BOARD_OK) {
            print_storage_info("storage after read_packet fail");
            print_qspi_snapshot("qspi after read_packet fail");
            return status;
        }

        debug_log_write_u32("read index packet = ", index);
        debug_log_write_u32("expected hash = ",
                            hash_buffer(packet_tx[index],
                                        NAND_STORAGE_PACKET_SIZE));
        debug_log_write_u32("actual hash = ",
                            hash_buffer(packet_rx,
                                        NAND_STORAGE_PACKET_SIZE));

        errors = compare_packets(packet_tx[index],
                                 packet_rx,
                                 NAND_STORAGE_PACKET_SIZE,
                                 index);
        debug_log_write_u32("packet errors = ", errors);

        total_errors += errors;
    }

    debug_log_write_u32("read by index total errors = ", total_errors);

    if (total_errors != 0U) {
        return BOARD_ERR_IO;
    }

    debug_log_write_u32("=== STORAGE READ BY INDEX TEST OK ===", 0U);

    return BOARD_OK;
}

static BoardStatus run_storage_read_next_test(void) {
    BoardStatus status;
    uint8_t has_packet;
    uint32_t index;
    uint32_t errors;
    uint32_t total_errors;

    debug_log_write_u32("=== STORAGE READ NEXT TEST START ===", 0U);

    status = nand_storage_open_read(TEST_BANK_ENUM, TEST_PACKET_COUNT);
    if (status != BOARD_OK) {
        return status;
    }

    total_errors = 0U;

    for (index = 0U; index < TEST_PACKET_COUNT; ++index) {
        clear_packet(packet_rx, NAND_STORAGE_PACKET_SIZE);
        has_packet = 0U;

        status = nand_storage_read_next_packet(packet_rx, &has_packet);
        if (status != BOARD_OK) {
            print_storage_info("storage after read_next fail");
            print_qspi_snapshot("qspi after read_next fail");
            return status;
        }

        if (has_packet == 0U) {
            return BOARD_ERR_IO;
        }

        debug_log_write_u32("read next packet = ", index);
        debug_log_write_u32("expected hash = ",
                            hash_buffer(packet_tx[index],
                                        NAND_STORAGE_PACKET_SIZE));
        debug_log_write_u32("actual hash = ",
                            hash_buffer(packet_rx,
                                        NAND_STORAGE_PACKET_SIZE));

        errors = compare_packets(packet_tx[index],
                                 packet_rx,
                                 NAND_STORAGE_PACKET_SIZE,
                                 index);
        debug_log_write_u32("packet errors = ", errors);

        total_errors += errors;
    }

    has_packet = 1U;

    status = nand_storage_read_next_packet(packet_rx, &has_packet);
    if (status != BOARD_OK) {
        return status;
    }

    if (has_packet != 0U) {
        return BOARD_ERR_IO;
    }

    debug_log_write_u32("read next total errors = ", total_errors);

    if (total_errors != 0U) {
        return BOARD_ERR_IO;
    }

    debug_log_write_u32("=== STORAGE READ NEXT TEST OK ===", 0U);

    return BOARD_OK;
}

static BoardStatus run_storage_capacity_test(void) {
    BoardStatus status;
    uint32_t capacity;

    capacity = 0U;

    status = nand_storage_get_capacity_packets(&capacity);
    if (status != BOARD_OK) {
        return status;
    }

    debug_log_write_u32("storage capacity packets = ", capacity);

    if (capacity == 0U) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus run_nand_storage_test(void) {
    BoardStatus status;
    NandMt29fId id;

    memset(&id, 0, sizeof(id));

    debug_log_write_u32("nand cache read mode = ",
                        (uint32_t)nand_mt29f_get_cache_read_mode());
    debug_log_write_u32("nand program load mode = ",
                        (uint32_t)nand_mt29f_get_program_load_mode());

    status = board_nand_power_on(TEST_BANK_ID);
    if (status != BOARD_OK) {
        return status;
    }

    debug_log_write_u32("board_nand_power_on OK", TEST_BANK_ID);

    status = board_nand_connect(TEST_BANK_ID);
    if (status != BOARD_OK) {
        return status;
    }

    debug_log_write_u32("board_nand_connect OK", TEST_BANK_ID);

    status = nand_mt29f_select_bank(TEST_BANK_ENUM);
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_mt29f_read_id(&id);
    if (status != BOARD_OK) {
        print_qspi_snapshot("snapshot after read id fail");
        return status;
    }

    debug_log_write_u32("id manufacturer = ", (uint32_t)id.manufacturer_id);
    debug_log_write_u32("id device = ", (uint32_t)id.device_id);

    status = nand_storage_init();
    if (status != BOARD_OK) {
        return status;
    }

    status = nand_storage_mount(TEST_BANK_ENUM);
    if (status != BOARD_OK) {
        print_qspi_snapshot("snapshot after storage mount fail");
        return status;
    }

    print_storage_info("storage after mount");

    status = run_storage_capacity_test();
    if (status != BOARD_OK) {
        return status;
    }

    status = prepare_test_packets();
    if (status != BOARD_OK) {
        return status;
    }

    status = run_storage_erase_range_test();
    if (status != BOARD_OK) {
        debug_log_write_u32("STORAGE ERASE RANGE TEST FAILED = ", (uint32_t)status);
        return status;
    }

    status = run_storage_write_test();
    if (status != BOARD_OK) {
        debug_log_write_u32("STORAGE WRITE TEST FAILED = ", (uint32_t)status);
        return status;
    }

    status = run_storage_read_by_index_test();
    if (status != BOARD_OK) {
        debug_log_write_u32("STORAGE READ BY INDEX TEST FAILED = ", (uint32_t)status);
        return status;
    }

    status = run_storage_read_next_test();
    if (status != BOARD_OK) {
        debug_log_write_u32("STORAGE READ NEXT TEST FAILED = ", (uint32_t)status);
        return status;
    }

    debug_log_write_u32(">>> NAND STORAGE TEST PASSED <<<", 0U);

    return BOARD_OK;
}

int main(void) {
    BoardStatus status;

    status = debug_log_init();
    if (status != BOARD_OK) {
        halt();
    }

    status = clock_init();
    if (status != BOARD_OK) {
        halt_on_error("clock_init failed = ", status);
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        halt_on_error("timebase_init failed = ", status);
    }

    status = board_init_hardware();
    if (status != BOARD_OK) {
        halt_on_error("board_init_hardware failed = ", status);
    }

    debug_log_write_u32("hardware init OK", 0U);

    status = run_nand_storage_test();
    if (status != BOARD_OK) {
        halt_on_error("nand storage test failed = ", status);
    }

    halt();
}
