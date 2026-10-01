#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "board_stub.h"
#include "mram_store.h"
#include "ni_stream.h"
#include "ni_writer.h"

#define STUB_CAPACITY 256U
#define POLL_GUARD    100000U

static NiWriter writer;
static NiStream stream;
static uint8_t words[NI_PACKET_CORE_WORDS * 2U];
static uint8_t readback[NI_PACKET_BYTES];

static void reset_board(void) {
    board_stub_reset_all();
    assert(board_nand_bad_block_scan_start(1U) == BOARD_OK);
    assert(board_nand_erase_start(1U) == BOARD_OK);
    memset(words, 0x3C, sizeof(words));
}

static MramStoreServiceData load_service(void) {
    MramStoreServiceData service;

    memset(&service, 0, sizeof(service));
    assert(mram_store_load_service_data(&service) == BOARD_OK);

    return service;
}

static void set_mram_progress(uint32_t count, uint16_t crc) {
    MramStoreServiceData service = load_service();

    service.nand1_packet_count = count;
    service.nand1_last_packet_crc = crc;
    assert(mram_store_save_service_data(&service) == BOARD_OK);
}

static void begin_and_recover(void) {
    uint32_t guard;

    assert(ni_writer_begin(&writer, 1U) == BOARD_OK);
    for (guard = 0U; (guard < 64U) && (ni_writer_state(&writer) == NI_WRITER_RECOVERING); ++guard) {
        (void)ni_writer_poll(&writer, &stream);
    }
    assert(ni_writer_state(&writer) != NI_WRITER_RECOVERING);
    assert(guard <= 20U);
}

static void start_stream(void) {
    ni_stream_begin(&stream, 7U, ni_writer_next_packet(&writer), ni_writer_last_crc(&writer));
}

static NiWriterState write_packets(uint32_t count) {
    uint32_t produced = 0U;
    uint32_t guard;
    NiWriterState state = ni_writer_state(&writer);

    for (guard = 0U; guard < POLL_GUARD; ++guard) {
        if ((produced < count) && (ni_stream_free_words(&stream) >= NI_PACKET_CORE_WORDS)) {
            assert(ni_stream_append(&stream, words, NI_PACKET_CORE_WORDS));
            ++produced;
        }

        state = ni_writer_poll(&writer, &stream);
        if ((state != NI_WRITER_READY) ||
            ((produced == count) && (ni_stream_ready_count(&stream) == 0U) && !writer.save_pending)) {
            break;
        }
    }

    return state;
}

static NiWriterState finish(void) {
    uint32_t guard;
    NiWriterState state = ni_writer_state(&writer);

    ni_stream_finish(&stream);
    ni_writer_request_finish(&writer);

    for (guard = 0U; (guard < POLL_GUARD) &&
                     ((state == NI_WRITER_READY) || (state == NI_WRITER_FLUSHING)); ++guard) {
        state = ni_writer_poll(&writer, &stream);
    }

    return state;
}

static uint16_t nand_packet_crc(uint32_t index) {
    assert(board_nand_read_packet(1U, index, readback) == BOARD_OK);
    return ni_packet_crc(readback);
}

static void fresh_bank_starts_at_zero(void) {
    reset_board();
    begin_and_recover();

    assert(ni_writer_state(&writer) == NI_WRITER_READY);
    assert(ni_writer_next_packet(&writer) == 0U);
    assert(ni_writer_last_crc(&writer) == 0U);
}

static void writes_in_order_and_saves_every_128(void) {
    MramStoreServiceData service;
    uint32_t i;

    reset_board();
    begin_and_recover();
    start_stream();

    assert(write_packets(130U) == NI_WRITER_READY);
    assert(ni_writer_next_packet(&writer) == 130U);

    service = load_service();
    assert(service.nand1_packet_count == 128U);
    assert(service.nand1_last_packet_crc == nand_packet_crc(127U));

    for (i = 0U; i < 130U; ++i) {
        assert(board_nand_read_packet(1U, i, readback) == BOARD_OK);
        assert(ni_packet_number(readback) == i);
        assert(ni_packet_session(readback) == 7U);
    }

    assert(finish() == NI_WRITER_DONE);
    assert(ni_writer_next_packet(&writer) == 130U);

    service = load_service();
    assert(service.nand1_packet_count == 130U);
    assert(service.nand1_last_packet_crc == nand_packet_crc(129U));
    assert(service.nand2_packet_count == 0U);
}

static void stops_when_bank_is_full(void) {
    MramStoreServiceData service;

    reset_board();
    begin_and_recover();
    start_stream();

    assert(write_packets(STUB_CAPACITY + 4U) == NI_WRITER_FULL);
    assert(ni_writer_next_packet(&writer) == STUB_CAPACITY);

    service = load_service();
    assert(service.nand1_packet_count == STUB_CAPACITY);
    assert(service.nand1_last_packet_crc == nand_packet_crc(STUB_CAPACITY - 1U));
}

static void full_bank_is_reported_at_begin(void) {
    reset_board();
    begin_and_recover();
    start_stream();
    assert(write_packets(STUB_CAPACITY) == NI_WRITER_FULL);

    begin_and_recover();
    assert(ni_writer_state(&writer) == NI_WRITER_FULL);
    assert(ni_writer_next_packet(&writer) == STUB_CAPACITY);
}

static void write_error_fails_and_marks_candidate(void) {
    BoardNandBlockMap map;

    reset_board();
    board_stub_set_nand_write_fail_at(1U, 5U);
    begin_and_recover();
    start_stream();

    assert(write_packets(10U) == NI_WRITER_FAILED);
    assert(ni_writer_last_error(&writer) == BOARD_ERR_IO);
    assert(ni_writer_next_packet(&writer) == 5U);

    assert(board_nand_get_block_map(1U, &map) == BOARD_OK);
    assert((map.candidate[0] & 0x01U) != 0U);
    assert((map.bad[0] & 0x01U) == 0U);
}

static void recovers_after_stale_mram(void) {
    reset_board();
    begin_and_recover();
    start_stream();
    assert(write_packets(40U) == NI_WRITER_READY);
    set_mram_progress(0U, 0U);

    begin_and_recover();
    assert(ni_writer_state(&writer) == NI_WRITER_READY);
    assert(ni_writer_next_packet(&writer) == 40U);
    assert(ni_writer_last_crc(&writer) == nand_packet_crc(39U));

    start_stream();
    assert(write_packets(3U) == NI_WRITER_READY);
    assert(board_nand_read_packet(1U, 40U, readback) == BOARD_OK);
    assert(ni_packet_number(readback) == 40U);
    assert(nand_packet_crc(39U) == (uint16_t)(readback[12] | (readback[13] << 8)));
}

static void uses_mram_crc_when_mram_is_current(void) {
    reset_board();
    begin_and_recover();
    start_stream();
    assert(write_packets(20U) == NI_WRITER_READY);
    assert(finish() == NI_WRITER_DONE);
    set_mram_progress(20U, 0xBEEFU);

    begin_and_recover();
    assert(ni_writer_next_packet(&writer) == 20U);
    assert(ni_writer_last_crc(&writer) == 0xBEEFU);
}

static void rejects_packet_number_gap(void) {
    reset_board();
    begin_and_recover();
    ni_stream_begin(&stream, 7U, 5U, 0U);

    assert(write_packets(1U) == NI_WRITER_FAILED);
    assert(ni_writer_last_error(&writer) == BOARD_ERR_INVALID_ARG);
}

static void mram_failure_fails_writer(void) {
    reset_board();
    begin_and_recover();
    start_stream();

    board_stub_set_mram_write_fail(true);
    assert(write_packets(NI_WRITER_MRAM_SAVE_PERIOD) == NI_WRITER_FAILED);
    board_stub_set_mram_write_fail(false);
}

int main(void) {
    fresh_bank_starts_at_zero();
    writes_in_order_and_saves_every_128();
    stops_when_bank_is_full();
    full_bank_is_reported_at_begin();
    write_error_fails_and_marks_candidate();
    recovers_after_stale_mram();
    uses_mram_crc_when_mram_is_current();
    rejects_packet_number_gap();
    mram_failure_fails_writer();

    return 0;
}
