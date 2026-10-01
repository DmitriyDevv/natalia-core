#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ni_stream.h"

#define CORE_BYTES   (NI_PACKET_CORE_WORDS * 2U)
#define CORE_OFFSET  (NI_PACKET_HEADER_WORDS * 2U)
#define PATTERN_WORDS 12000U

static NiStream stream;
static uint8_t pattern[PATTERN_WORDS * 2U];
static uint8_t collected[32U * CORE_BYTES];

static uint16_t reference_crc(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFFU;
    size_t i;
    int bit;

    for (i = 0U; i < size; ++i) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static uint16_t word_at(const uint8_t* data, size_t index) {
    return (uint16_t)((uint16_t)data[index * 2U] | ((uint16_t)data[(index * 2U) + 1U] << 8));
}

static void fill_pattern(void) {
    size_t i;

    for (i = 0U; i < PATTERN_WORDS; ++i) {
        pattern[i * 2U] = (uint8_t)(i & 0xFFU);
        pattern[(i * 2U) + 1U] = (uint8_t)((i >> 8) & 0x7FU);
    }
}

static void check_packet(const uint8_t* packet, uint16_t session, uint32_t number,
                         uint16_t previous_crc) {
    assert(word_at(packet, 0U) == 0x46FFU);
    assert(word_at(packet, 1U) == 0xC9D7U);
    assert(word_at(packet, 2U) == 0xA5B3U);
    assert(word_at(packet, 3U) == session);
    assert(word_at(packet, 4U) == (uint16_t)(number & 0xFFFFU));
    assert(word_at(packet, 5U) == (uint16_t)(number >> 16));
    assert(word_at(packet, 6U) == previous_crc);
    assert(word_at(packet, 1023U) == reference_crc(&packet[CORE_OFFSET], CORE_BYTES));
    assert(ni_packet_session(packet) == session);
    assert(ni_packet_number(packet) == number);
    assert(ni_packet_crc(packet) == word_at(packet, 1023U));
}

static size_t drain_checked(uint16_t session, uint32_t* number, uint16_t* previous_crc,
                            size_t collected_packets) {
    const uint8_t* packet;

    while ((packet = ni_stream_peek(&stream)) != NULL) {
        check_packet(packet, session, *number, *previous_crc);
        memcpy(&collected[collected_packets * CORE_BYTES], &packet[CORE_OFFSET], CORE_BYTES);
        *previous_crc = ni_packet_crc(packet);
        ++(*number);
        ++collected_packets;
        ni_stream_pop(&stream);
    }

    return collected_packets;
}

static void short_session_is_padded(void) {
    const uint8_t* packet;
    size_t i;

    fill_pattern();
    ni_stream_begin(&stream, 7U, 5U, 0x1234U);

    assert(ni_stream_append(&stream, pattern, 10U));
    assert(ni_stream_ready_count(&stream) == 0U);
    assert(ni_stream_pending_words(&stream) == 10U);

    ni_stream_finish(&stream);
    assert(ni_stream_ready_count(&stream) == 1U);
    assert(ni_stream_pending_words(&stream) == 0U);

    packet = ni_stream_peek(&stream);
    assert(packet != NULL);
    check_packet(packet, 7U, 5U, 0x1234U);
    assert(memcmp(&packet[CORE_OFFSET], pattern, 20U) == 0);
    for (i = 17U; i < 1023U; ++i) {
        assert(word_at(packet, i) == 0xAAAAU);
    }

    assert(ni_stream_next_packet_number(&stream) == 6U);
    assert(ni_stream_previous_crc(&stream) == ni_packet_crc(packet));
    assert(!ni_stream_append(&stream, pattern, 1U));
}

static void packet_number_high_word(void) {
    ni_stream_begin(&stream, 1U, 0x00012345UL, 0U);
    assert(ni_stream_append(&stream, pattern, NI_PACKET_CORE_WORDS));
    assert(ni_stream_ready_count(&stream) == 1U);
    check_packet(ni_stream_peek(&stream), 1U, 0x00012345UL, 0U);
}

static void exact_fill_is_not_padded(void) {
    ni_stream_begin(&stream, 2U, 0U, 0U);
    assert(ni_stream_append(&stream, pattern, NI_PACKET_CORE_WORDS));
    assert(ni_stream_ready_count(&stream) == 1U);
    assert(ni_stream_pending_words(&stream) == 0U);

    ni_stream_finish(&stream);
    assert(ni_stream_ready_count(&stream) == 1U);
}

static void formats_cross_packets_continuously(void) {
    uint32_t number = 100U;
    uint16_t previous_crc = 0xBEEFU;
    size_t packets = 0U;

    fill_pattern();
    ni_stream_begin(&stream, 3U, number, previous_crc);

    assert(ni_stream_append(&stream, pattern, 1000U));
    assert(ni_stream_append(&stream, &pattern[1000U * 2U], 2066U));
    assert(ni_stream_ready_count(&stream) == 3U);
    packets = drain_checked(3U, &number, &previous_crc, packets);

    assert(ni_stream_append(&stream, &pattern[3066U * 2U], 15U));
    ni_stream_finish(&stream);
    packets = drain_checked(3U, &number, &previous_crc, packets);

    assert(packets == 4U);
    assert(memcmp(collected, pattern, 3081U * 2U) == 0);
    assert(word_at(collected, 3081U) == 0xAAAAU);
    assert(word_at(collected, (4U * NI_PACKET_CORE_WORDS) - 1U) == 0xAAAAU);
    assert(ni_stream_next_packet_number(&stream) == 104U);
}

static void append_is_all_or_nothing(void) {
    size_t capacity = NI_STREAM_FIFO_PACKETS * NI_PACKET_CORE_WORDS;

    ni_stream_begin(&stream, 4U, 0U, 0U);
    assert(ni_stream_free_words(&stream) == capacity);

    assert(ni_stream_append(&stream, pattern, 100U));
    assert(ni_stream_free_words(&stream) == (capacity - 100U));
    assert(!ni_stream_append(&stream, pattern, capacity - 99U));
    assert(ni_stream_pending_words(&stream) == 100U);
    assert(ni_stream_ready_count(&stream) == 0U);

    assert(ni_stream_append(&stream, pattern, capacity - 100U));
    assert(ni_stream_ready_count(&stream) == NI_STREAM_FIFO_PACKETS);
    assert(ni_stream_free_words(&stream) == 0U);
    assert(!ni_stream_append(&stream, pattern, 1U));

    ni_stream_pop(&stream);
    assert(ni_stream_free_words(&stream) == NI_PACKET_CORE_WORDS);
    assert(!ni_stream_append(&stream, pattern, NI_PACKET_CORE_WORDS + 1U));
    assert(ni_stream_append(&stream, pattern, NI_PACKET_CORE_WORDS));
    assert(ni_stream_ready_count(&stream) == NI_STREAM_FIFO_PACKETS);
    assert(ni_stream_append(&stream, pattern, 0U));
}

static void fifo_wraps_in_order(void) {
    uint32_t number = 0U;
    uint16_t previous_crc = 0U;
    size_t round;
    size_t packets = 0U;

    ni_stream_begin(&stream, 5U, 0U, 0U);

    for (round = 0U; round < 5U; ++round) {
        assert(ni_stream_append(&stream, pattern, 5U * NI_PACKET_CORE_WORDS));
        packets = drain_checked(5U, &number, &previous_crc, packets);
    }

    assert(packets == 25U);
    assert(number == 25U);
}

static void finish_without_data_emits_nothing(void) {
    ni_stream_begin(&stream, 6U, 9U, 0x1111U);
    ni_stream_finish(&stream);
    assert(ni_stream_ready_count(&stream) == 0U);
    assert(ni_stream_next_packet_number(&stream) == 9U);
    assert(ni_stream_previous_crc(&stream) == 0x1111U);
}

static void discard_drops_partial_packet(void) {
    ni_stream_begin(&stream, 8U, 0U, 0x2222U);
    assert(ni_stream_append(&stream, pattern, NI_PACKET_CORE_WORDS + 50U));
    assert(ni_stream_ready_count(&stream) == 1U);

    ni_stream_discard_partial(&stream);
    assert(ni_stream_pending_words(&stream) == 0U);
    assert(ni_stream_ready_count(&stream) == 1U);
    assert(ni_stream_next_packet_number(&stream) == 1U);
    assert(!ni_stream_append(&stream, pattern, 1U));
}

static void rejects_use_before_begin(void) {
    memset(&stream, 0, sizeof(stream));
    assert(!ni_stream_append(&stream, pattern, 1U));
    assert(ni_stream_free_words(&stream) == 0U);
    assert(ni_stream_peek(&stream) == NULL);

    ni_stream_begin(&stream, 1U, 0U, 0U);
    assert(!ni_stream_append(&stream, NULL, 1U));
}

int main(void) {
    fill_pattern();

    short_session_is_padded();
    packet_number_high_word();
    exact_fill_is_not_padded();
    formats_cross_packets_continuously();
    append_is_all_or_nothing();
    fifo_wraps_in_order();
    finish_without_data_emits_nothing();
    discard_drops_partial_packet();
    rejects_use_before_begin();

    return 0;
}
