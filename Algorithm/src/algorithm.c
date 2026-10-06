#include "algorithm.h"

#include <stddef.h>
#include <string.h>

#include "alarm.h"
#include "alarm_monitor.h"
#include "board_api.h"
#include "dump_mode_config.h"
#include "event_queue.h"
#include "observe.h"
#include "test_mode_config.h"

#ifndef ALGORITHM_EVENTS_PER_POLL
#define ALGORITHM_EVENTS_PER_POLL (8U)
#endif

static uint8_t erase_bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static void finish_erase_step(SystemContext* ctx) {
    (void)ctx;
    (void)system_event_queue_push_back_type(EVENT_ERASE_DONE);
}

static void erase_mode_wait_step(SystemContext* ctx) {
    uint8_t is_done = 0U;
    BoardStatus status = board_nand_erase_is_done(erase_bank_id(ctx->erase.bank), &is_done);

    if (status != BOARD_OK) {
        alarm_raise(ctx, ALARM_NAND_PR);
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

static void erase_mode_poll(SystemContext* ctx) {
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

static uint32_t test_packet_address(uint32_t packet_index) {
    return packet_index * TEST_MODE_PACKET_SIZE;
}

static uint32_t test_global_packet(const TestContext* test) {
    return (test->block_index * TEST_MODE_PACKETS_PER_BLOCK) + test->packet_in_block;
}

static uint8_t make_test_pattern_byte(const TestContext* test, uint32_t address, size_t offset) {
    uint32_t value = address;

    value ^= (uint32_t)offset;
    value ^= test->test_mask;
    value ^= 0xA5A5A5A5UL;
    value ^= value >> 16U;
    value ^= value >> 8U;

    return (uint8_t)(value & 0xFFU);
}

static void fill_test_pattern(TestContext* test) {
    uint32_t address = test_packet_address(test_global_packet(test));
    size_t index;

    for (index = 0U; index < TEST_MODE_PACKET_SIZE; ++index) {
        test->write_buffer[index] = make_test_pattern_byte(test, address, index);
    }
}

static void finish_test_step(SystemContext* ctx) {
    (void)ctx;
    (void)system_event_queue_push_back_type(EVENT_TEST_DONE);
}

static void fail_test_step(SystemContext* ctx, uint32_t status_flag) {
    alarm_raise(ctx, ALARM_NAND_PR);
    ctx->test.result_status |= status_flag;
    ctx->test.operation_failed = true;
    ctx->test.result_valid = false;
    ctx->test.write_started = false;
    ctx->test.stage = TEST_STAGE_SAVE;
    finish_test_step(ctx);
}

static void test_mode_erase_step(SystemContext* ctx) {
    TestContext* test = &ctx->test;
    BoardStatus status;
    uint8_t is_done = 0U;

    status = board_nand_erase_is_done(test_bank_id(test->bank), &is_done);
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_ERASE_ERROR);
        return;
    }

    if (is_done == 0U) {
        return;
    }

    if (test->final_erase) {
        test->stage = TEST_STAGE_SAVE;
        finish_test_step(ctx);
        return;
    }

    if (test->total_blocks == 0U) {
        test->stage = TEST_STAGE_SAVE;
        finish_test_step(ctx);
        return;
    }

    status = board_nand_open_write(test_bank_id(test->bank), 0U);
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_WRITE_ERROR);
        return;
    }

    test->stage = TEST_STAGE_WRITE;
}

static void test_mode_write_step(SystemContext* ctx) {
    TestContext* test = &ctx->test;
    uint32_t packet = test_global_packet(test);
    BoardStatus status;
    uint8_t is_done = 0U;

    if (!test->write_started) {
        fill_test_pattern(test);

        status = board_nand_open_write(test_bank_id(test->bank), packet);
        if (status != BOARD_OK) {
            fail_test_step(ctx, TEST_RESULT_STATUS_NAND_WRITE_ERROR);
            return;
        }

        status = board_nand_write_packet(test_bank_id(test->bank), test->write_buffer);
        if (status != BOARD_OK) {
            fail_test_step(ctx, TEST_RESULT_STATUS_NAND_WRITE_ERROR);
            return;
        }

        test->current_address = test_packet_address(packet);
        test->write_started = true;
    }

    status = board_nand_write_flush(test_bank_id(test->bank), &is_done);
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_WRITE_ERROR);
        return;
    }

    if (is_done == 0U) {
        return;
    }

    test->write_started = false;
    test->stage = TEST_STAGE_READ;
}

static void test_mode_read_step(SystemContext* ctx) {
    TestContext* test = &ctx->test;
    uint32_t packet = test_global_packet(test);
    BoardStatus status;

    status = board_nand_open_read(test_bank_id(test->bank), packet + 1U);
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_READ_ERROR);
        return;
    }

    (void)memset(test->read_buffer, 0, sizeof(test->read_buffer));

    status = board_nand_read_packet(test_bank_id(test->bank), packet, test->read_buffer);
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_READ_ERROR);
        return;
    }

    test->current_address = test_packet_address(packet);
    test->stage = TEST_STAGE_COMPARE;
}

static void test_mode_compare_step(SystemContext* ctx) {
    TestContext* test = &ctx->test;
    uint32_t packet = test_global_packet(test);
    uint32_t address = test_packet_address(packet);
    uint32_t errors = 0U;
    uint64_t block_sum;
    size_t index;
    BoardStatus status;

    for (index = 0U; index < TEST_MODE_PACKET_SIZE; ++index) {
        if (test->write_buffer[index] != test->read_buffer[index]) {
            ++errors;

            if (test->failed_address == TEST_MODE_FAILED_ADDRESS_NONE) {
                test->failed_address = address + (uint32_t)index;
            }
        }
    }

    block_sum = (uint64_t)test->nerr[test->block_index] + (uint64_t)errors;
    test->nerr[test->block_index] =
        (block_sum > TEST_MODE_NERR_MAX) ? (uint32_t)TEST_MODE_NERR_MAX : (uint32_t)block_sum;
    test->total_errors += errors;

    if (errors > 0U) {
        test->result_status |= TEST_RESULT_STATUS_COMPARE_MISMATCH;
    }

    ++test->packet_in_block;

    if (test->packet_in_block < TEST_MODE_PACKETS_PER_BLOCK) {
        test->stage = TEST_STAGE_WRITE;
        return;
    }

    test->packet_in_block = 0U;
    ++test->block_index;

    if (test->block_index < test->total_blocks) {
        test->stage = TEST_STAGE_WRITE;
        return;
    }

    test->final_erase = true;

    status = board_nand_erase_start(test_bank_id(test->bank));
    if (status != BOARD_OK) {
        fail_test_step(ctx, TEST_RESULT_STATUS_NAND_ERASE_ERROR);
        return;
    }

    test->stage = TEST_STAGE_ERASE;
}

static void test_mode_poll(SystemContext* ctx) {
    switch (ctx->test.stage) {
    case TEST_STAGE_ERASE:
        test_mode_erase_step(ctx);
        break;
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

static void finish_dump_step(SystemContext* ctx) {
    (void)ctx;
    (void)system_event_queue_push_back_type(EVENT_DUMP_DONE);
}

static void fail_dump_step(SystemContext* ctx) {
    ctx->dump.operation_failed = true;
    ctx->dump.stage = DUMP_STAGE_FINISH_ALARM;
    finish_dump_step(ctx);
}

static void dump_mode_read_step(SystemContext *ctx) {
    DumpContext *dump = &ctx->dump;
    uint32_t packet_index;
    uint32_t address;
    BoardStatus status;

    if (dump->bytes_done >= dump->size) {
        dump->stage = DUMP_STAGE_FINISH_OK;
        finish_dump_step(ctx);
        return;
    }

    address = dump->start_address + dump->bytes_done;

    if ((address % DUMP_MODE_PACKET_SIZE) != 0U) {
        fail_dump_step(ctx);
        return;
    }

    packet_index = address / DUMP_MODE_PACKET_SIZE;

    dump->packet_size = DUMP_MODE_PACKET_SIZE;
    dump->send_offset = 0U;
    dump->usb_retry_count = 0U;

    status = board_nand_read_packet(dump_bank_id(dump->bank), packet_index, dump->packet_buffer);
    if ((status != BOARD_OK) && (status != BOARD_ERR_CRC)) {
        alarm_raise(ctx, ALARM_NAND_PR);
        fail_dump_step(ctx);
        return;
    }

    dump->stage = DUMP_STAGE_SEND;
}

static void dump_mode_send_step(SystemContext* ctx) {
    DumpContext* dump = &ctx->dump;
    size_t bytes_written = 0U;
    size_t bytes_left;
    BoardStatus status;

    if (dump->send_offset >= dump->packet_size) {
        dump->stage = DUMP_STAGE_CHECK;
        return;
    }

    if (!dump->link_ready) {
        uint8_t ready = 0U;

        if ((board_data_link_ready(&ready) != BOARD_OK) || (ready == 0U)) {
            return;
        }
        dump->link_ready = true;
    }

    bytes_left = (size_t)(dump->packet_size - dump->send_offset);
    dump->tx_attempted = true;
    status = board_data_write(&dump->packet_buffer[dump->send_offset], bytes_left, &bytes_written);

    if ((status == BOARD_OK) && (bytes_written == 0U)) {
        return;
    }

    if ((status != BOARD_OK) || (bytes_written > bytes_left)) {
        if (dump->usb_retry_count < DUMP_MODE_USB_MAX_RETRIES) {
            ++dump->usb_retry_count;
            return;
        }

        alarm_raise(ctx, ALARM_USB_PR);
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

static void dump_mode_check_step(SystemContext* ctx) {
    DumpContext* dump = &ctx->dump;

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

static void dump_mode_poll(SystemContext* ctx) {
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

static void collect_nand_power_fault(SystemContext *ctx, uint8_t bank_id) {
    uint8_t fault = 0U;

    if ((board_nand_take_power_fault(bank_id, &fault) == BOARD_OK) && (fault != 0U)) {
        alarm_raise(ctx, ALARM_NAND_PS);
    }
}

void algorithm_collect_hw_events(SystemContext *ctx) {
    uint32_t count;

    if (ctx == NULL) {
        return;
    }

    collect_nand_power_fault(ctx, 1U);
    collect_nand_power_fault(ctx, 2U);

    count = 0U;
    if (board_rtc_take_1hz_events(&count) == BOARD_OK) {
        while (count > 0U) {
            (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
            --count;
        }
    }

    if (ctx->state == STATE_DUMP) {
        uint8_t link_fault = 0U;

        if ((board_data_link_take_fault(&link_fault) == BOARD_OK) && (link_fault != 0U)) {
            alarm_raise(ctx, ALARM_USB_VBUS);
        }
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
    } else if (ctx->state == STATE_OBSERVE) {
        observe_poll(ctx);
    } else if (ctx->state == STATE_DUMP) {
        dump_mode_poll(ctx);
    }
}

void algorithm_process_events(SystemContext *ctx) {
    SystemEvent event;
    uint32_t processed;

    if (ctx == NULL) {
        return;
    }

    for (processed = 0U; processed < ALGORITHM_EVENTS_PER_POLL; ++processed) {
        if (!system_event_queue_pop(&event)) {
            break;
        }

        (void)handle_event(ctx, &event);
    }
}
