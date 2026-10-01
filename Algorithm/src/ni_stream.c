#include "ni_stream.h"

#include <string.h>

#include "crc16.h"

#define NI_STREAM_BYTES(words) ((words) * 2U)

static void ni_stream_put_word(uint8_t* packet, size_t index, uint16_t value) {
    packet[NI_STREAM_BYTES(index)] = (uint8_t)(value & 0x00FFU);
    packet[NI_STREAM_BYTES(index) + 1U] = (uint8_t)((value >> 8) & 0x00FFU);
}

static uint16_t ni_stream_get_word(const uint8_t* packet, size_t index) {
    return (uint16_t)((uint16_t)packet[NI_STREAM_BYTES(index)] |
                      ((uint16_t)packet[NI_STREAM_BYTES(index) + 1U] << 8));
}

static uint8_t* ni_stream_current_slot(NiStream* stream) {
    return stream->slots[(stream->head + stream->ready_count) % NI_STREAM_FIFO_PACKETS];
}

static void ni_stream_open_current(NiStream* stream) {
    uint8_t* packet = ni_stream_current_slot(stream);

    ni_stream_put_word(packet, 0U, NI_PACKET_MARKER_1);
    ni_stream_put_word(packet, 1U, NI_PACKET_MARKER_2);
    ni_stream_put_word(packet, 2U, NI_PACKET_MARKER_3);
    ni_stream_put_word(packet, 3U, stream->session_id);
    ni_stream_put_word(packet, 4U, (uint16_t)(stream->next_packet_number & 0xFFFFU));
    ni_stream_put_word(packet, 5U, (uint16_t)((stream->next_packet_number >> 16) & 0xFFFFU));
    ni_stream_put_word(packet, 6U, stream->previous_crc);
}

static void ni_stream_close_current(NiStream* stream) {
    uint8_t* packet = ni_stream_current_slot(stream);
    uint16_t crc = crc16_ccitt(&packet[NI_STREAM_BYTES(NI_PACKET_HEADER_WORDS)],
                               NI_STREAM_BYTES(NI_PACKET_CORE_WORDS));

    ni_stream_put_word(packet, NI_PACKET_WORDS - 1U, crc);

    stream->previous_crc = crc;
    ++stream->next_packet_number;
    ++stream->ready_count;
    stream->current_words = 0U;
}

void ni_stream_begin(NiStream* stream, uint16_t session_id,
                     uint32_t first_packet_number, uint16_t previous_crc) {
    if (stream == NULL) {
        return;
    }

    stream->head = 0U;
    stream->ready_count = 0U;
    stream->current_words = 0U;
    stream->next_packet_number = first_packet_number;
    stream->previous_crc = previous_crc;
    stream->session_id = session_id;
    stream->active = true;
}

size_t ni_stream_free_words(const NiStream* stream) {
    if ((stream == NULL) || !stream->active) {
        return 0U;
    }

    return ((NI_STREAM_FIFO_PACKETS - stream->ready_count) * NI_PACKET_CORE_WORDS) -
           stream->current_words;
}

bool ni_stream_append(NiStream* stream, const uint8_t* data, size_t words) {
    size_t chunk;

    if ((stream == NULL) || !stream->active || ((data == NULL) && (words > 0U))) {
        return false;
    }

    if (words > ni_stream_free_words(stream)) {
        return false;
    }

    while (words > 0U) {
        uint8_t* packet;

        if (stream->current_words == 0U) {
            ni_stream_open_current(stream);
        }

        packet = ni_stream_current_slot(stream);
        chunk = NI_PACKET_CORE_WORDS - stream->current_words;
        if (chunk > words) {
            chunk = words;
        }

        (void)memcpy(&packet[NI_STREAM_BYTES(NI_PACKET_HEADER_WORDS + stream->current_words)],
                     data, NI_STREAM_BYTES(chunk));

        stream->current_words += chunk;
        data += NI_STREAM_BYTES(chunk);
        words -= chunk;

        if (stream->current_words == NI_PACKET_CORE_WORDS) {
            ni_stream_close_current(stream);
        }
    }

    return true;
}

void ni_stream_finish(NiStream* stream) {
    uint8_t* packet;
    size_t index;

    if ((stream == NULL) || !stream->active) {
        return;
    }

    if (stream->current_words > 0U) {
        packet = ni_stream_current_slot(stream);

        for (index = NI_PACKET_HEADER_WORDS + stream->current_words;
             index < (NI_PACKET_HEADER_WORDS + NI_PACKET_CORE_WORDS); ++index) {
            ni_stream_put_word(packet, index, NI_PACKET_PAD_WORD);
        }

        ni_stream_close_current(stream);
    }

    stream->active = false;
}

void ni_stream_discard_partial(NiStream* stream) {
    if (stream == NULL) {
        return;
    }

    stream->current_words = 0U;
    stream->active = false;
}

size_t ni_stream_ready_count(const NiStream* stream) {
    return (stream == NULL) ? 0U : stream->ready_count;
}

const uint8_t* ni_stream_peek(const NiStream* stream) {
    if ((stream == NULL) || (stream->ready_count == 0U)) {
        return NULL;
    }

    return stream->slots[stream->head];
}

void ni_stream_pop(NiStream* stream) {
    if ((stream == NULL) || (stream->ready_count == 0U)) {
        return;
    }

    stream->head = (stream->head + 1U) % NI_STREAM_FIFO_PACKETS;
    --stream->ready_count;
}

size_t ni_stream_pending_words(const NiStream* stream) {
    return (stream == NULL) ? 0U : stream->current_words;
}

uint32_t ni_stream_next_packet_number(const NiStream* stream) {
    return (stream == NULL) ? 0U : stream->next_packet_number;
}

uint16_t ni_stream_previous_crc(const NiStream* stream) {
    return (stream == NULL) ? 0U : stream->previous_crc;
}

uint16_t ni_packet_session(const uint8_t* packet) {
    return (packet == NULL) ? 0U : ni_stream_get_word(packet, 3U);
}

uint32_t ni_packet_number(const uint8_t* packet) {
    if (packet == NULL) {
        return 0U;
    }

    return (uint32_t)ni_stream_get_word(packet, 4U) |
           ((uint32_t)ni_stream_get_word(packet, 5U) << 16);
}

uint16_t ni_packet_crc(const uint8_t* packet) {
    return (packet == NULL) ? 0U : ni_stream_get_word(packet, NI_PACKET_WORDS - 1U);
}
