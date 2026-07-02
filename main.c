#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "algorithm.h"
#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "dump_mode_config.h"
#include "qspi.h"
#include "state.h"
#include "status.h"
#include "timebase.h"

#define TEST_BANK_ID 2U
#define OBSERVE_DUMP_PACKET_COUNT 4U
#define OBSERVE_DUMP_PERIOD_TICKS 1U
#define OBSERVE_DUMP_SIZE (OBSERVE_DUMP_PACKET_COUNT * DUMP_MODE_PACKET_SIZE)
#define TEST_TIMEOUT_POLLS 12000000UL
#define TEST_PROGRESS_STEP 64U

static SystemContext ctx;

static void halt(void) {
    while (1) {}
}

static void halt_on_error(const char* text, BoardStatus status) {
    debug_log_write_u32(text, (uint32_t)status);
    halt();
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

static NandBank test_state_bank(uint8_t bank_id) {
    if (bank_id == 1U) {
        return NAND_BANK_1;
    }

    return NAND_BANK_2;
}

static BoardStatus wait_board_erase_done(uint8_t bank_id) {
    BoardStatus status;
    uint8_t is_done;
    uint32_t polls_left;
    uint32_t poll_count;

    is_done = 0U;
    polls_left = TEST_TIMEOUT_POLLS;
    poll_count = 0U;

    while (is_done == 0U) {
        if (polls_left == 0U) {
            debug_log_write_u32("board erase timeout", 0U);
            print_qspi_snapshot("qspi after board erase timeout");
            return BOARD_ERR_TIMEOUT;
        }

        status = board_nand_erase_is_done(bank_id, &is_done);
        if (status != BOARD_OK) {
            debug_log_write_u32("board erase poll failed = ", (uint32_t)status);
            print_qspi_snapshot("qspi after board erase poll fail");
            return status;
        }

        if ((poll_count % TEST_PROGRESS_STEP) == 0U) {
            debug_log_write_u32("board erase poll = ", poll_count);
        }

        ++poll_count;
        --polls_left;
    }

    debug_log_write_u32("board erase done polls = ", poll_count);

    return BOARD_OK;
}

static BoardStatus erase_bank_before_test(uint8_t bank_id) {
    BoardStatus status;

    status = board_nand_power_on(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_connect(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = board_nand_erase_start(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    status = wait_board_erase_done(bank_id);
    if (status != BOARD_OK) {
        return status;
    }

    return board_nand_power_off(bank_id);
}

static void init_test_context(SystemContext* context) {
    (void)memset(context, 0, sizeof(*context));

    context->state = STATE_DUTY;
    context->previous_state = STATE_INIT;

    context->nand1.bank = NAND_BANK_1;
    context->nand1.is_powered = false;
    context->nand1.is_connected = false;
    context->nand1.is_full = false;

    context->nand2.bank = NAND_BANK_2;
    context->nand2.is_powered = false;
    context->nand2.is_connected = false;
    context->nand2.is_full = false;

    context->alarm_status = 0U;
    context->alarm_mask = 0U;
    context->masked_alarm = 0U;
}

static BoardStatus start_algorithm_observe(SystemContext* context, uint8_t bank_id) {
    SystemEvent event;
    SystemState result_state;

    (void)memset(&event, 0, sizeof(event));

    event.type = EVENT_CMD_OBSERVE_START;
    event.msg_id = 0x0005U;
    event.command.observe_start.bank = test_state_bank(bank_id);
    event.command.observe_start.power_after_done = POWER_AFTER_DONE_KEEP;
    event.command.observe_start.acquisition_period_ticks = OBSERVE_DUMP_PERIOD_TICKS;

    result_state = handle_event(context, &event);

    debug_log_write_u32("handle observe result state = ", (uint32_t)result_state);
    debug_log_write_u32("ctx state = ", (uint32_t)context->state);
    debug_log_write_u32("observe stage = ", (uint32_t)context->observe.stage);

    if (context->state != STATE_OBSERVE) {
        return BOARD_ERR_IO;
    }

    if (context->observe.stage != OBSERVE_STAGE_ACTIVE) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus send_ped_trigger(SystemContext* context) {
    SystemEvent event;

    (void)memset(&event, 0, sizeof(event));

    event.type = EVENT_PED_TRIGGER;
    event.msg_id = 0U;

    (void)handle_event(context, &event);

    if (context->observe.operation_failed) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus wait_observe_committed(SystemContext* context, uint32_t target_count) {
    uint32_t polls_left;
    uint32_t poll_count;

    polls_left = TEST_TIMEOUT_POLLS;
    poll_count = 0U;

    while (context->observe.committed_packet_count < target_count) {
        if (polls_left == 0U) {
            debug_log_write_u32("observe write timeout", 0U);
            debug_log_write_u32("observe stage = ", (uint32_t)context->observe.stage);
            debug_log_write_u32("observe packet index = ", context->observe.packet_index);
            debug_log_write_u32("observe committed = ", context->observe.committed_packet_count);
            debug_log_write_u32("observe events = ", context->observe.events_written);
            print_qspi_snapshot("qspi after observe timeout");
            return BOARD_ERR_TIMEOUT;
        }

        algorithm_poll(context);

        if ((poll_count % TEST_PROGRESS_STEP) == 0U) {
            debug_log_write_u32("observe poll = ", poll_count);
            debug_log_write_u32("observe stage = ", (uint32_t)context->observe.stage);
            debug_log_write_u32("observe packet index = ", context->observe.packet_index);
            debug_log_write_u32("observe committed = ", context->observe.committed_packet_count);
            debug_log_write_u32("observe events = ", context->observe.events_written);
        }

        if (context->state != STATE_OBSERVE) {
            return BOARD_ERR_IO;
        }

        if (context->observe.operation_failed) {
            return BOARD_ERR_IO;
        }

        ++poll_count;
        --polls_left;
    }

    return BOARD_OK;
}

static BoardStatus stop_algorithm_observe(SystemContext* context) {
    SystemEvent event;
    SystemState result_state;

    (void)memset(&event, 0, sizeof(event));

    event.type = EVENT_CMD_DUTY;
    event.msg_id = 0x0007U;

    result_state = handle_event(context, &event);

    debug_log_write_u32("stop observe result state = ", (uint32_t)result_state);
    debug_log_write_u32("state after observe stop = ", (uint32_t)context->state);
    debug_log_write_u32("observe stage = ", (uint32_t)context->observe.stage);
    debug_log_write_u32("observe packet index = ", context->observe.packet_index);
    debug_log_write_u32("observe committed = ", context->observe.committed_packet_count);
    debug_log_write_u32("observe events = ", context->observe.events_written);

    if (context->state != STATE_DUTY) {
        return BOARD_ERR_IO;
    }

    if (context->observe.operation_failed) {
        return BOARD_ERR_IO;
    }

    if (context->observe.committed_packet_count != OBSERVE_DUMP_PACKET_COUNT) {
        return BOARD_ERR_IO;
    }

    if (context->observe.events_written != OBSERVE_DUMP_PACKET_COUNT) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus run_observe_phase(SystemContext* context) {
    BoardStatus status;
    uint32_t packet_index;

    status = start_algorithm_observe(context, TEST_BANK_ID);
    if (status != BOARD_OK) {
        debug_log_write_u32("observe start failed = ", (uint32_t)status);
        print_qspi_snapshot("qspi after observe start fail");
        return status;
    }

    for (packet_index = 0U; packet_index < OBSERVE_DUMP_PACKET_COUNT; ++packet_index) {
        status = send_ped_trigger(context);
        if (status != BOARD_OK) {
            debug_log_write_u32("ped trigger failed = ", packet_index);
            return status;
        }

        status = wait_observe_committed(context, packet_index + 1U);
        if (status != BOARD_OK) {
            debug_log_write_u32("observe commit failed = ", packet_index);
            return status;
        }

        debug_log_write_u32("observe committed packet = ", packet_index);
    }

    return stop_algorithm_observe(context);
}

static BoardStatus start_algorithm_dump(SystemContext* context, uint8_t bank_id) {
    SystemEvent event;
    SystemState result_state;

    (void)memset(&event, 0, sizeof(event));

    event.type = EVENT_CMD_DUMP;
    event.msg_id = 0x0006U;
    event.command.dump.bank = test_state_bank(bank_id);
    event.command.dump.power_after_done = POWER_AFTER_DONE_KEEP;
    event.command.dump.start_address = 0U;
    event.command.dump.size = OBSERVE_DUMP_SIZE;

    result_state = handle_event(context, &event);

    debug_log_write_u32("handle dump result state = ", (uint32_t)result_state);
    debug_log_write_u32("ctx state = ", (uint32_t)context->state);
    debug_log_write_u32("dump stage = ", (uint32_t)context->dump.stage);

    if (context->state != STATE_DUMP) {
        return BOARD_ERR_IO;
    }

    if (context->dump.stage != DUMP_STAGE_READ) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus wait_algorithm_dump_done(SystemContext* context) {
    uint32_t polls_left;
    uint32_t poll_count;

    polls_left = TEST_TIMEOUT_POLLS;
    poll_count = 0U;

    while (context->state == STATE_DUMP) {
        if (polls_left == 0U) {
            debug_log_write_u32("algorithm dump timeout", 0U);
            debug_log_write_u32("dump stage = ", (uint32_t)context->dump.stage);
            debug_log_write_u32("dump bytes_done = ", context->dump.bytes_done);
            debug_log_write_u32("dump bytes_written = ", context->usb.bytes_written);
            print_qspi_snapshot("qspi after dump timeout");
            return BOARD_ERR_TIMEOUT;
        }

        algorithm_poll(context);

        if ((poll_count % TEST_PROGRESS_STEP) == 0U) {
            debug_log_write_u32("algorithm dump poll = ", poll_count);
            debug_log_write_u32("algorithm state = ", (uint32_t)context->state);
            debug_log_write_u32("dump stage = ", (uint32_t)context->dump.stage);
            debug_log_write_u32("dump bytes_done = ", context->dump.bytes_done);
            debug_log_write_u32("dump bytes_written = ", context->usb.bytes_written);
            debug_log_write_u32("dump last packet = ", context->dump.last_dumped_packet);
        }

        ++poll_count;
        --polls_left;
    }

    debug_log_write_u32("algorithm dump done polls = ", poll_count);
    debug_log_write_u32("final state = ", (uint32_t)context->state);
    debug_log_write_u32("final dump stage = ", (uint32_t)context->dump.stage);
    debug_log_write_u32("dump operation_failed = ", (uint32_t)context->dump.operation_failed);
    debug_log_write_u32("dump bytes_done = ", context->dump.bytes_done);
    debug_log_write_u32("dump bytes_written = ", context->usb.bytes_written);
    debug_log_write_u32("dump last packet = ", context->dump.last_dumped_packet);

    if (context->state != STATE_DUTY) {
        return BOARD_ERR_IO;
    }

    if (context->dump.operation_failed) {
        return BOARD_ERR_IO;
    }

    if (context->dump.bytes_done != OBSERVE_DUMP_SIZE) {
        return BOARD_ERR_IO;
    }

    if (context->usb.bytes_written != OBSERVE_DUMP_SIZE) {
        return BOARD_ERR_IO;
    }

    if (context->dump.last_dumped_packet != OBSERVE_DUMP_PACKET_COUNT) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus run_dump_phase(SystemContext* context) {
    BoardStatus status;
    uint32_t usb_bytes_written;
    uint32_t usb_expected_bytes;
    uint32_t usb_error_count;

    usb_bytes_written = 0U;
    usb_expected_bytes = 0U;
    usb_error_count = 0U;

    status = board_usb_test_capture_start(OBSERVE_DUMP_PACKET_COUNT,
                                          OBSERVE_DUMP_PERIOD_TICKS);
    if (status != BOARD_OK) {
        return status;
    }

    status = start_algorithm_dump(context, TEST_BANK_ID);
    if (status != BOARD_OK) {
        debug_log_write_u32("dump start failed = ", (uint32_t)status);
        print_qspi_snapshot("qspi after dump start fail");
        return status;
    }

    status = wait_algorithm_dump_done(context);
    if (status != BOARD_OK) {
        debug_log_write_u32("dump wait failed = ", (uint32_t)status);
        return status;
    }

    status = board_usb_test_capture_get_result(&usb_bytes_written,
                                               &usb_expected_bytes,
                                               &usb_error_count);
    if (status != BOARD_OK) {
        return status;
    }

    debug_log_write_u32("usb captured bytes = ", usb_bytes_written);
    debug_log_write_u32("usb expected bytes = ", usb_expected_bytes);
    debug_log_write_u32("usb compare errors = ", usb_error_count);

    if (usb_bytes_written != usb_expected_bytes) {
        return BOARD_ERR_IO;
    }

    if (usb_error_count != 0U) {
        return BOARD_ERR_IO;
    }

    return BOARD_OK;
}

static BoardStatus run_observe_dump_chain_test(void) {
    BoardStatus status;

    debug_log_write_u32("=== OBSERVE TO DUMP CHAIN START ===", 0U);
    debug_log_write_u32("test bank id = ", TEST_BANK_ID);
    debug_log_write_u32("packet count = ", OBSERVE_DUMP_PACKET_COUNT);
    debug_log_write_u32("dump size = ", OBSERVE_DUMP_SIZE);

    status = erase_bank_before_test(TEST_BANK_ID);
    if (status != BOARD_OK) {
        debug_log_write_u32("erase before chain failed = ", (uint32_t)status);
        print_qspi_snapshot("qspi after erase before chain fail");
        return status;
    }

    init_test_context(&ctx);

    status = run_observe_phase(&ctx);
    if (status != BOARD_OK) {
        debug_log_write_u32("observe phase failed = ", (uint32_t)status);
        return status;
    }

    status = run_dump_phase(&ctx);
    if (status != BOARD_OK) {
        debug_log_write_u32("dump phase failed = ", (uint32_t)status);
        return status;
    }

    status = board_nand_power_off(TEST_BANK_ID);
    if (status != BOARD_OK) {
        return status;
    }

    debug_log_write_u32(">>> OBSERVE TO DUMP CHAIN PASSED <<<", 0U);

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

    status = run_observe_dump_chain_test();
    if (status != BOARD_OK) {
        halt_on_error("observe dump chain failed = ", status);
    }

    halt();
}
