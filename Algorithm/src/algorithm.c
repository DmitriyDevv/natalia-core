#include "algorithm.h"

#include <stddef.h>

#include "board_api.h"
#include "dump_mode_config.h"
#include "test_mode_config.h"

static uint8_t erase_bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static void finish_erase_step(SystemContext *ctx) {
    const SystemEvent done_event = {
        .type = EVENT_ERASE_DONE,
        .msg_id = 0U
    };

    (void)handle_event(ctx, &done_event);
}

static void erase_mode_wait_step(SystemContext *ctx) {
    uint8_t is_done = 0U;
    BoardStatus status = board_nand_erase_is_done(erase_bank_id(ctx->erase.bank), &is_done);

    if (status != BOARD_OK) {
        ctx->erase.operation_failed = true;
        ctx->erase.stage = ERASE_STAGE_FINISH_ALARM;
        finish_erase_step(ctx);
        return;
    }

    if (is_done != 0U) {
        ctx->erase.stage = ERASE_STAGE_FINISH_OK;
        finish_erase_step(ctx);
    }
}

static void erase_mode_poll(SystemContext *ctx) {
    switch (ctx->erase.stage) {
        case ERASE_STAGE_WAIT:
            erase_mode_wait_step(ctx);
            break;
        case ERASE_STAGE_IDLE:
        case ERASE_STAGE_ENTER:
        case ERASE_STAGE_START:
        case ERASE_STAGE_FINISH_OK:
        case ERASE_STAGE_FINISH_CMD:
        case ERASE_STAGE_FINISH_ALARM:
        default:
            break;
    }
}

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

static uint8_t dump_bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static uint32_t dump_next_packet_size(const DumpContext *dump) {
    uint32_t remaining = dump->size - dump->bytes_done;

    if (remaining > DUMP_MODE_PACKET_SIZE) {
        return DUMP_MODE_PACKET_SIZE;
    }

    return remaining;
}

static void finish_dump_step(SystemContext *ctx) {
    const SystemEvent done_event = {
        .type = EVENT_DUMP_DONE,
        .msg_id = 0U
    };

    (void)handle_event(ctx, &done_event);
}

static void fail_dump_step(SystemContext *ctx) {
    ctx->dump.operation_failed = true;
    ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
    finish_dump_step(ctx);
}

static void dump_mode_read_step(SystemContext *ctx) {
    DumpContext *dump = &ctx->dump;
    uint32_t address;

    if (dump->bytes_done >= dump->size) {
        dump->stage = DUMP_STAGE_CHECK;
        return;
    }

    dump->packet_size = dump_next_packet_size(dump);
    dump->send_offset = 0U;
    dump->usb_retry_count = 0U;
    address = dump->start_address + dump->bytes_done;

    if (board_nand_read(dump_bank_id(dump->bank), address, dump->packet_buffer, dump->packet_size) != BOARD_OK) {
        fail_dump_step(ctx);
        return;
    }

    dump->stage = DUMP_STAGE_SEND;
}

static void dump_mode_send_step(SystemContext *ctx) {
    DumpContext *dump = &ctx->dump;
    size_t bytes_written = 0U;
    size_t bytes_left;
    BoardStatus status;

    if (dump->send_offset >= dump->packet_size) {
        dump->stage = DUMP_STAGE_CHECK;
        return;
    }

    bytes_left = (size_t)(dump->packet_size - dump->send_offset);
    status = board_usb_write(&dump->packet_buffer[dump->send_offset], bytes_left, &bytes_written);
    if ((status != BOARD_OK) || (bytes_written == 0U) || (bytes_written > bytes_left)) {
        if (dump->usb_retry_count < DUMP_MODE_USB_MAX_RETRIES) {
            ++dump->usb_retry_count;
            return;
        }

        fail_dump_step(ctx);
        return;
    }

    dump->usb_retry_count = 0U;
    dump->send_offset += (uint32_t)bytes_written;
    ctx->usb.bytes_written += (uint32_t)bytes_written;

    if (dump->send_offset >= dump->packet_size) {
        dump->stage = DUMP_STAGE_CHECK;
    }
}

static void dump_mode_check_step(SystemContext *ctx) {
    DumpContext *dump = &ctx->dump;

    dump->bytes_done += dump->packet_size;
    ++dump->last_dumped_packet;
    dump->packet_size = 0U;
    dump->send_offset = 0U;
    dump->usb_retry_count = 0U;

    if (dump->bytes_done >= dump->size) {
        dump->stage = DUMP_STAGE_FINISH_OK;
        finish_dump_step(ctx);
        return;
    }

    dump->stage = DUMP_STAGE_READ;
}

static void dump_mode_poll(SystemContext *ctx) {
    switch (ctx->dump.stage) {
        case DUMP_STAGE_READ:
            dump_mode_read_step(ctx);
            break;
        case DUMP_STAGE_SEND:
            dump_mode_send_step(ctx);
            break;
        case DUMP_STAGE_CHECK:
            dump_mode_check_step(ctx);
            break;
        case DUMP_STAGE_IDLE:
        case DUMP_STAGE_ENTER:
        case DUMP_STAGE_FINISH_OK:
        case DUMP_STAGE_FINISH_CMD:
        case DUMP_STAGE_FINISH_ALARM:
        default:
            break;
    }
}

void algorithm_poll(SystemContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == STATE_ERASE) {
        erase_mode_poll(ctx);
    } else if (ctx->state == STATE_TEST) {
        test_mode_poll(ctx);
    } else if (ctx->state == STATE_DUMP) {
        dump_mode_poll(ctx);
    }
}
