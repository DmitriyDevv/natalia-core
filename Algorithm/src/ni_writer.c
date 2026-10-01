#include "ni_writer.h"

#include <string.h>

#include "mram_store.h"

static void ni_writer_fail(NiWriter* writer, BoardStatus status) {
    writer->last_error = status;
    writer->state = NI_WRITER_FAILED;
}

static bool ni_writer_packet_is_erased(const uint8_t* packet) {
    size_t i;

    for (i = 0U; i < NI_PACKET_BYTES; ++i) {
        if (packet[i] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static uint32_t ni_writer_limit(const NiWriter* writer) {
    return (writer->capacity < NI_WRITER_MAX_PACKET_COUNT) ? writer->capacity : NI_WRITER_MAX_PACKET_COUNT;
}

static BoardStatus ni_writer_save_progress(NiWriter* writer) {
    MramStoreServiceData service_data;
    BoardStatus status;

    (void)memset(&service_data, 0, sizeof(service_data));

    status = mram_store_load_service_data(&service_data);
    if (status != BOARD_OK) {
        return status;
    }

    if (writer->bank_id == 1U) {
        service_data.nand1_packet_count = writer->next_packet;
        service_data.nand1_last_packet_crc = writer->last_crc;
    } else {
        service_data.nand2_packet_count = writer->next_packet;
        service_data.nand2_last_packet_crc = writer->last_crc;
    }

    status = mram_store_save_service_data(&service_data);
    if (status == BOARD_OK) {
        writer->mram_count = writer->next_packet;
        writer->mram_crc = writer->last_crc;
    }

    return status;
}

static void ni_writer_open_for_write(NiWriter* writer) {
    BoardStatus status;

    if (writer->next_packet >= ni_writer_limit(writer)) {
        writer->state = NI_WRITER_FULL;
        return;
    }

    status = board_nand_open_write(writer->bank_id, writer->next_packet);
    if (status != BOARD_OK) {
        ni_writer_fail(writer, status);
        return;
    }

    writer->state = NI_WRITER_READY;
}

static void ni_writer_recover_step(NiWriter* writer) {
    uint32_t mid;
    BoardStatus status;

    if (writer->crc_lookup) {
        status = board_nand_read_packet(writer->bank_id, writer->next_packet - 1U, writer->probe);
        if (status != BOARD_OK) {
            ni_writer_fail(writer, status);
            return;
        }

        writer->last_crc = ni_packet_crc(writer->probe);
        writer->crc_lookup = false;
        ni_writer_open_for_write(writer);
        return;
    }

    if (writer->search_low < writer->search_high) {
        mid = writer->search_low + ((writer->search_high - writer->search_low) / 2U);

        status = board_nand_read_packet(writer->bank_id, mid, writer->probe);
        if (status != BOARD_OK) {
            ni_writer_fail(writer, status);
            return;
        }

        if (ni_writer_packet_is_erased(writer->probe)) {
            writer->search_high = mid;
        } else {
            writer->search_low = mid + 1U;
        }

        return;
    }

    writer->next_packet = writer->search_low;

    if (writer->next_packet == writer->mram_count) {
        writer->last_crc = (writer->next_packet == 0U) ? 0U : writer->mram_crc;
        ni_writer_open_for_write(writer);
        return;
    }

    writer->crc_lookup = true;
}

static void ni_writer_write_step(NiWriter* writer, NiStream* stream) {
    const uint8_t* packet;
    uint8_t is_done = 0U;
    BoardStatus status;

    if (writer->save_pending) {
        status = board_nand_write_flush(writer->bank_id, &is_done);
        if (status != BOARD_OK) {
            ni_writer_fail(writer, status);
            return;
        }

        if (is_done == 0U) {
            return;
        }

        status = ni_writer_save_progress(writer);
        if (status != BOARD_OK) {
            ni_writer_fail(writer, status);
            return;
        }

        writer->save_pending = false;

        if (writer->next_packet >= ni_writer_limit(writer)) {
            writer->state = NI_WRITER_FULL;
        }

        return;
    }

    packet = ni_stream_peek(stream);

    if (packet == NULL) {
        if (writer->finish_requested && (ni_stream_pending_words(stream) == 0U)) {
            writer->state = NI_WRITER_FLUSHING;
            return;
        }

        status = board_nand_write_poll(writer->bank_id, &is_done);
        if (status != BOARD_OK) {
            ni_writer_fail(writer, status);
        }
        return;
    }

    if (ni_packet_number(packet) != writer->next_packet) {
        ni_writer_fail(writer, BOARD_ERR_INVALID_ARG);
        return;
    }

    status = board_nand_write_packet(writer->bank_id, packet);
    if (status == BOARD_ERR_BUSY) {
        (void)board_nand_write_poll(writer->bank_id, &is_done);
        return;
    }

    if (status != BOARD_OK) {
        ni_writer_fail(writer, status);
        return;
    }

    writer->last_crc = ni_packet_crc(packet);
    ni_stream_pop(stream);
    ++writer->next_packet;

    if (((writer->next_packet % NI_WRITER_MRAM_SAVE_PERIOD) == 0U) ||
        (writer->next_packet >= ni_writer_limit(writer))) {
        writer->save_pending = true;
    }
}

static void ni_writer_flush_step(NiWriter* writer) {
    uint8_t is_done = 0U;
    BoardStatus status;

    status = board_nand_write_flush(writer->bank_id, &is_done);
    if (status != BOARD_OK) {
        ni_writer_fail(writer, status);
        return;
    }

    if (is_done == 0U) {
        return;
    }

    status = ni_writer_save_progress(writer);
    if (status != BOARD_OK) {
        ni_writer_fail(writer, status);
        return;
    }

    writer->state = NI_WRITER_DONE;
}

BoardStatus ni_writer_begin(NiWriter* writer, uint8_t bank_id) {
    MramStoreServiceData service_data;
    BoardStatus status;

    if ((writer == NULL) || ((bank_id != 1U) && (bank_id != 2U))) {
        return BOARD_ERR_INVALID_ARG;
    }

    (void)memset(writer, 0, sizeof(*writer));
    writer->bank_id = bank_id;
    writer->state = NI_WRITER_FAILED;

    (void)memset(&service_data, 0, sizeof(service_data));
    status = mram_store_load_service_data(&service_data);
    if (status != BOARD_OK) {
        writer->last_error = status;
        return status;
    }

    writer->mram_count = (bank_id == 1U) ? service_data.nand1_packet_count : service_data.nand2_packet_count;
    writer->mram_crc = (bank_id == 1U) ? service_data.nand1_last_packet_crc : service_data.nand2_last_packet_crc;

    status = board_nand_get_capacity_packets(bank_id, &writer->capacity);
    if (status != BOARD_OK) {
        writer->last_error = status;
        return status;
    }

    status = board_nand_open_read(bank_id, ni_writer_limit(writer));
    if (status != BOARD_OK) {
        writer->last_error = status;
        return status;
    }

    writer->search_high = ni_writer_limit(writer);
    writer->search_low = (writer->mram_count < writer->search_high) ? writer->mram_count : writer->search_high;
    writer->state = NI_WRITER_RECOVERING;

    return BOARD_OK;
}

NiWriterState ni_writer_poll(NiWriter* writer, NiStream* stream) {
    if ((writer == NULL) || (stream == NULL)) {
        return NI_WRITER_FAILED;
    }

    switch (writer->state) {
    case NI_WRITER_RECOVERING:
        ni_writer_recover_step(writer);
        break;

    case NI_WRITER_READY:
        ni_writer_write_step(writer, stream);
        break;

    case NI_WRITER_FLUSHING:
        ni_writer_flush_step(writer);
        break;

    case NI_WRITER_IDLE:
    case NI_WRITER_DONE:
    case NI_WRITER_FULL:
    case NI_WRITER_FAILED:
    default:
        break;
    }

    return writer->state;
}

void ni_writer_request_finish(NiWriter* writer) {
    if (writer != NULL) {
        writer->finish_requested = true;
    }
}

NiWriterState ni_writer_state(const NiWriter* writer) {
    return (writer == NULL) ? NI_WRITER_FAILED : writer->state;
}

uint32_t ni_writer_next_packet(const NiWriter* writer) {
    return (writer == NULL) ? 0U : writer->next_packet;
}

uint16_t ni_writer_last_crc(const NiWriter* writer) {
    return (writer == NULL) ? 0U : writer->last_crc;
}

BoardStatus ni_writer_last_error(const NiWriter* writer) {
    return (writer == NULL) ? BOARD_ERR_INVALID_ARG : writer->last_error;
}
