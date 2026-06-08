#include "algorithm.h"

#include <stddef.h>

#include "board_api.h"
#include "test_mode_config.h"

static uint8_t test_bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static uint32_t test_block_address(uint32_t block_index) {
    return block_index * TEST_MODE_BLOCK_SIZE;
}

static uint8_t make_test_pattern_byte(const TestContext *test, uint32_t address, size_t offset) {
    uint32_t value = address;
    value ^= (uint32_t)offset;
    value ^= test->test_mask;
    value ^= 0xA5A5A5A5UL;
    value ^= value >> 16U;
    value ^= value >> 8U;
    return (uint8_t)(value & 0xFFU);
}

static void fill_test_pattern(TestContext *test) {
    uint32_t address = test_block_address(test->block_index);

    for (size_t i = 0U; i < TEST_MODE_BLOCK_SIZE; ++i) {
        test->write_buffer[i] = make_test_pattern_byte(test, address, i);
    }
}

static void finish_test_step(SystemContext *ctx) {
    const SystemEvent done_event = {
        .type = EVENT_TEST_DONE,
        .msg_id = 0U
    };

    (void)handle_event(ctx, &done_event);
}

static void fail_test_step(SystemContext *ctx, uint32_t status_flag) {
    ctx->test.result_status |= status_flag;
    ctx->test.operation_failed = true;
    ctx->test.result_valid = false;
    ctx->test.stage = TEST_STAGE_SAVE;
    finish_test_step(ctx);
}

static void test_mode_write_step(SystemContext *ctx) {
    TestContext *test = &ctx->test;
    uint32_t address = test_block_address(test->block_index);

    fill_test_pattern(test);

    if (board_nand_write(test_bank_id(test->bank), address, test->write_buffer, TEST_MODE_BLOCK_SIZE) != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_WRITE_ERROR);
        return;
    }

    test->current_address = address;
    test->stage = TEST_STAGE_READ;
}

static void test_mode_read_step(SystemContext *ctx) {
    TestContext *test = &ctx->test;
    uint32_t address = test_block_address(test->block_index);

    if (board_nand_read(test_bank_id(test->bank), address, test->read_buffer, TEST_MODE_BLOCK_SIZE) != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_READ_ERROR);
        return;
    }

    test->current_address = address;
    test->stage = TEST_STAGE_COMPARE;
}

static void test_mode_compare_step(SystemContext *ctx) {
    TestContext *test = &ctx->test;
    uint32_t address = test_block_address(test->block_index);
    uint32_t errors = 0U;

    for (size_t i = 0U; i < TEST_MODE_BLOCK_SIZE; ++i) {
        if (test->write_buffer[i] != test->read_buffer[i]) {
            ++errors;
            if (test->failed_address == TEST_MODE_FAILED_ADDRESS_NONE) {
                test->failed_address = address + (uint32_t)i;
            }
        }
    }

    test->nerr[test->block_index] = (uint16_t)errors;
    test->total_errors += errors;
    if (errors > 0U) {
        test->result_status |= TEST_RESULT_STATUS_COMPARE_MISMATCH;
    }

    ++test->block_index;
    if (test->block_index >= TEST_MODE_BLOCK_COUNT) {
        test->stage = TEST_STAGE_SAVE;
        finish_test_step(ctx);
        return;
    }

    test->stage = TEST_STAGE_WRITE;
}

static void test_mode_poll(SystemContext *ctx) {
    switch (ctx->test.stage) {
        case TEST_STAGE_WRITE:
            test_mode_write_step(ctx);
            break;
        case TEST_STAGE_READ:
            test_mode_read_step(ctx);
            break;
        case TEST_STAGE_COMPARE:
            test_mode_compare_step(ctx);
            break;
        case TEST_STAGE_IDLE:
        case TEST_STAGE_ENTER:
        case TEST_STAGE_SAVE:
        case TEST_STAGE_ERASE:
        case TEST_STAGE_FINISH_OK:
        case TEST_STAGE_FINISH_CMD:
        case TEST_STAGE_FINISH_ALARM:
        default:
            break;
    }
}

void algorithm_poll(SystemContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == STATE_TEST) {
        test_mode_poll(ctx);
    }
}
