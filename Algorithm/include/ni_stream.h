#ifndef NATALIA_CORE_NI_STREAM_H
#define NATALIA_CORE_NI_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NI_PACKET_WORDS        1024U
#define NI_PACKET_BYTES        (NI_PACKET_WORDS * 2U)
#define NI_PACKET_HEADER_WORDS 7U
#define NI_PACKET_CORE_WORDS   (NI_PACKET_WORDS - NI_PACKET_HEADER_WORDS - 1U)
#define NI_PACKET_MARKER_1     0x46FFU
#define NI_PACKET_MARKER_2     0xC9D7U
#define NI_PACKET_MARKER_3     0xA5B3U
#define NI_PACKET_PAD_WORD     0xAAAAU
#define NI_STREAM_FIFO_PACKETS 8U

typedef struct {
    uint8_t slots[NI_STREAM_FIFO_PACKETS][NI_PACKET_BYTES];
    size_t head;
    size_t ready_count;
    size_t current_words;
    uint32_t next_packet_number;
    uint16_t previous_crc;
    uint16_t session_id;
    bool active;
} NiStream;

void ni_stream_begin(NiStream* stream, uint16_t session_id,
                     uint32_t first_packet_number, uint16_t previous_crc);
size_t ni_stream_free_words(const NiStream* stream);
bool ni_stream_append(NiStream* stream, const uint8_t* data, size_t words);
void ni_stream_finish(NiStream* stream);
void ni_stream_discard_partial(NiStream* stream);

size_t ni_stream_ready_count(const NiStream* stream);
const uint8_t* ni_stream_peek(const NiStream* stream);
void ni_stream_pop(NiStream* stream);

size_t ni_stream_pending_words(const NiStream* stream);
uint32_t ni_stream_next_packet_number(const NiStream* stream);
uint16_t ni_stream_previous_crc(const NiStream* stream);

uint16_t ni_packet_session(const uint8_t* packet);
uint32_t ni_packet_number(const uint8_t* packet);
uint16_t ni_packet_crc(const uint8_t* packet);

#endif /* NATALIA_CORE_NI_STREAM_H */
