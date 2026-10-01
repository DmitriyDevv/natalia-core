#ifndef NATALIA_CORE_NI_WRITER_H
#define NATALIA_CORE_NI_WRITER_H

#include <stdbool.h>
#include <stdint.h>

#include "board_api.h"
#include "ni_stream.h"

#define NI_WRITER_MRAM_SAVE_PERIOD  128U
#define NI_WRITER_MAX_PACKET_COUNT  262144UL

typedef enum {
    NI_WRITER_IDLE = 0,
    NI_WRITER_RECOVERING,
    NI_WRITER_READY,
    NI_WRITER_FLUSHING,
    NI_WRITER_DONE,
    NI_WRITER_FULL,
    NI_WRITER_FAILED
} NiWriterState;

typedef struct {
    NiWriterState state;
    uint8_t bank_id;
    uint32_t capacity;
    uint32_t next_packet;
    uint16_t last_crc;
    uint32_t mram_count;
    uint16_t mram_crc;
    uint32_t search_low;
    uint32_t search_high;
    bool crc_lookup;
    bool save_pending;
    bool finish_requested;
    BoardStatus last_error;
    uint8_t probe[NI_PACKET_BYTES];
} NiWriter;

BoardStatus ni_writer_begin(NiWriter* writer, uint8_t bank_id);
NiWriterState ni_writer_poll(NiWriter* writer, NiStream* stream);
void ni_writer_request_finish(NiWriter* writer);

NiWriterState ni_writer_state(const NiWriter* writer);
uint32_t ni_writer_next_packet(const NiWriter* writer);
uint16_t ni_writer_last_crc(const NiWriter* writer);
BoardStatus ni_writer_last_error(const NiWriter* writer);

#endif /* NATALIA_CORE_NI_WRITER_H */
