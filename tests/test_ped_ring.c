#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board_api.h"
#include "board_stub.h"
#include "ped_reg_ring.h"

static PedRegRing ring;

static PedRegRecord make_record(uint8_t kind, uint32_t sequence) {
    PedRegRecord record;
    size_t i;

    record.kind = kind;
    record.flags = (uint8_t)(sequence & 0x03U);
    for (i = 0U; i < PED_REG_RECORD_DATA_WORDS; ++i) {
        record.data[i] = (uint16_t)((sequence + i) & 0xFFFFU);
    }
    record.rtc_seconds = sequence;

    return record;
}

static void check_record(const PedRegRecord* record, uint8_t kind, uint32_t sequence) {
    size_t i;

    assert(record->kind == kind);
    assert(record->flags == (uint8_t)(sequence & 0x03U));
    for (i = 0U; i < PED_REG_RECORD_DATA_WORDS; ++i) {
        assert(record->data[i] == (uint16_t)((sequence + i) & 0xFFFFU));
    }
    assert(record->rtc_seconds == sequence);
}

static void empty_ring(void) {
    PedRegRecord record;

    ped_reg_ring_reset(&ring);
    assert(ped_reg_ring_count(&ring) == 0U);
    assert(ped_reg_ring_has_event_room(&ring));
    assert(!ped_reg_ring_pop(&ring, &record));
    assert(ped_reg_ring_high_water(&ring) == 0U);
    assert(ped_reg_ring_seconds_lost(&ring) == 0U);
}

static void keeps_order_of_events_and_seconds(void) {
    PedRegRecord record;
    uint32_t i;

    ped_reg_ring_reset(&ring);

    for (i = 0U; i < 5U; ++i) {
        record = make_record(PED_REG_RECORD_EVENT, i);
        assert(ped_reg_ring_push_event(&ring, &record));
    }
    record = make_record(PED_REG_RECORD_SECOND, 5U);
    assert(ped_reg_ring_push_second(&ring, &record));
    record = make_record(PED_REG_RECORD_EVENT, 6U);
    assert(ped_reg_ring_push_event(&ring, &record));

    assert(ped_reg_ring_count(&ring) == 7U);

    for (i = 0U; i < 7U; ++i) {
        assert(ped_reg_ring_pop(&ring, &record));
        check_record(&record, (i == 5U) ? PED_REG_RECORD_SECOND : PED_REG_RECORD_EVENT, i);
    }

    assert(!ped_reg_ring_pop(&ring, &record));
    assert(ped_reg_ring_high_water(&ring) == 7U);
}

static void reserve_is_kept_for_seconds(void) {
    PedRegRecord record = make_record(PED_REG_RECORD_EVENT, 0U);
    uint32_t accepted = 0U;
    uint32_t i;

    ped_reg_ring_reset(&ring);

    while (ped_reg_ring_push_event(&ring, &record)) {
        ++accepted;
    }

    assert(accepted == (PED_REG_RING_CAPACITY - PED_REG_RING_SECOND_RESERVE));
    assert(!ped_reg_ring_has_event_room(&ring));

    record = make_record(PED_REG_RECORD_SECOND, 1U);
    for (i = 0U; i < PED_REG_RING_SECOND_RESERVE; ++i) {
        assert(ped_reg_ring_push_second(&ring, &record));
    }

    assert(ped_reg_ring_count(&ring) == PED_REG_RING_CAPACITY);
    assert(!ped_reg_ring_push_second(&ring, &record));
    assert(!ped_reg_ring_push_second(&ring, &record));
    assert(ped_reg_ring_seconds_lost(&ring) == 2U);
    assert(ped_reg_ring_high_water(&ring) == PED_REG_RING_CAPACITY);

    assert(ped_reg_ring_pop(&ring, &record));
    assert(!ped_reg_ring_has_event_room(&ring));
    assert(ped_reg_ring_push_second(&ring, &record));
}

static void wraps_around_many_times(void) {
    PedRegRecord record;
    uint32_t written = 0U;
    uint32_t read = 0U;
    uint32_t round;

    ped_reg_ring_reset(&ring);

    for (round = 0U; round < (3U * PED_REG_RING_CAPACITY); ++round) {
        record = make_record(PED_REG_RECORD_EVENT, written);
        assert(ped_reg_ring_push_event(&ring, &record));
        ++written;

        if ((round % 3U) != 0U) {
            assert(ped_reg_ring_pop(&ring, &record));
            check_record(&record, PED_REG_RECORD_EVENT, read);
            ++read;
        }

        if (!ped_reg_ring_has_event_room(&ring)) {
            while (ped_reg_ring_pop(&ring, &record)) {
                check_record(&record, PED_REG_RECORD_EVENT, read);
                ++read;
            }
        }
    }

    while (ped_reg_ring_pop(&ring, &record)) {
        check_record(&record, PED_REG_RECORD_EVENT, read);
        ++read;
    }

    assert(read == written);
    assert(ped_reg_ring_count(&ring) == 0U);
}

/* Batch pop returns records in order, splits a batch at the end of the storage
 * and never returns more than asked or more than stored. */
static void pop_many_wraps_and_keeps_order(void) {
    static PedRegRecord batch[100];
    PedRegRecord record;
    uint32_t written = 0U;
    uint32_t read = 0U;
    uint32_t round;
    uint32_t count;
    uint32_t i;

    ped_reg_ring_reset(&ring);
    assert(ped_reg_ring_pop_many(&ring, batch, 100U) == 0U);
    assert(ped_reg_ring_pop_many(NULL, batch, 100U) == 0U);
    assert(ped_reg_ring_pop_many(&ring, NULL, 100U) == 0U);

    for (round = 0U; round < 200U; ++round) {
        while (ped_reg_ring_has_event_room(&ring) && ((written - read) < 1500U)) {
            record = make_record(PED_REG_RECORD_EVENT, written);
            assert(ped_reg_ring_push_event(&ring, &record));
            ++written;
        }

        count = ped_reg_ring_pop_many(&ring, batch, 37U + (round % 64U));
        assert(count == (37U + (round % 64U)));
        for (i = 0U; i < count; ++i) {
            check_record(&batch[i], PED_REG_RECORD_EVENT, read);
            ++read;
        }
    }

    while ((count = ped_reg_ring_pop_many(&ring, batch, 100U)) != 0U) {
        for (i = 0U; i < count; ++i) {
            check_record(&batch[i], PED_REG_RECORD_EVENT, read);
            ++read;
        }
    }

    assert(read == written);
    assert(written > (3U * PED_REG_RING_CAPACITY));
    assert(ped_reg_ring_count(&ring) == 0U);
}

static void rejects_invalid_arguments(void) {
    PedRegRecord record = make_record(PED_REG_RECORD_EVENT, 0U);

    ped_reg_ring_reset(&ring);
    assert(!ped_reg_ring_push_event(&ring, NULL));
    assert(!ped_reg_ring_push_second(&ring, NULL));
    assert(!ped_reg_ring_push_event(NULL, &record));
    assert(!ped_reg_ring_pop(&ring, NULL));
    assert(ped_reg_ring_count(NULL) == 0U);
}

static void stub_returns_records_in_order(void) {
    BoardPedRecord in;
    BoardPedRecord out[4];
    size_t count = 99U;
    uint32_t i;

    board_stub_reset_all();

    assert(board_ped_take_records(out, 4U, &count) == BOARD_OK);
    assert(count == 0U);

    for (i = 0U; i < 3U; ++i) {
        memset(&in, 0, sizeof(in));
        in.kind = (i == 1U) ? BOARD_PED_RECORD_SECOND : BOARD_PED_RECORD_EVENT;
        in.data[0] = (uint16_t)(100U + i);
        in.rtc_seconds = i;
        assert(board_stub_push_ped_record(&in));
    }

    assert(board_ped_take_records(out, 2U, &count) == BOARD_OK);
    assert(count == 2U);
    assert(out[0].data[0] == 100U);
    assert(out[1].kind == BOARD_PED_RECORD_SECOND);

    assert(board_ped_take_records(out, 4U, &count) == BOARD_OK);
    assert(count == 1U);
    assert(out[0].data[0] == 102U);

    assert(board_ped_take_records(NULL, 1U, &count) == BOARD_ERR_INVALID_ARG);
    assert(board_ped_take_records(out, 1U, NULL) == BOARD_ERR_INVALID_ARG);
}

static void stub_faults_writes_and_acquisition(void) {
    uint32_t faults = 0U;
    uint8_t address = 0U;
    uint16_t value = 0U;
    BoardPedStats stats;

    board_stub_reset_all();

    board_stub_set_ped_faults(BOARD_PED_FAULT_POWER | BOARD_PED_FAULT_STATUS);
    assert(board_ped_take_faults(&faults) == BOARD_OK);
    assert(faults == (BOARD_PED_FAULT_POWER | BOARD_PED_FAULT_STATUS));
    assert(board_ped_take_faults(&faults) == BOARD_OK);
    assert(faults == 0U);

    assert(board_ped_write_register(0x80U, 0x1234U) == BOARD_OK);
    assert(board_ped_write_register(0x81U, 0x0007U) == BOARD_OK);
    assert(board_stub_ped_register_write_count() == 2U);
    assert(board_stub_ped_register_write_at(1U, &address, &value));
    assert(address == 0x81U);
    assert(value == 0x0007U);
    assert(!board_stub_ped_register_write_at(2U, &address, &value));

    board_stub_set_ped_write_status(BOARD_ERR_NOT_READY);
    assert(board_ped_write_register(0x80U, 0U) == BOARD_ERR_NOT_READY);
    assert(board_stub_ped_register_write_count() == 2U);

    assert(!board_stub_ped_acquisition_active());
    assert(board_ped_acquisition_start() == BOARD_OK);
    assert(board_stub_ped_acquisition_active());
    assert(board_ped_acquisition_stop() == BOARD_OK);
    assert(!board_stub_ped_acquisition_active());

    assert(board_ped_get_stats(&stats) == BOARD_OK);
    assert(stats.ring_count == 0U);

    board_stub_reset_all();
    assert(board_stub_ped_register_write_count() == 0U);
    assert(board_ped_write_register(0x80U, 0U) == BOARD_OK);
}

int main(void) {
    empty_ring();
    keeps_order_of_events_and_seconds();
    reserve_is_kept_for_seconds();
    wraps_around_many_times();
    pop_many_wraps_and_keeps_order();
    rejects_invalid_arguments();
    stub_returns_records_in_order();
    stub_faults_writes_and_acquisition();

    return 0;
}
