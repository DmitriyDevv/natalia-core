#ifndef NATALIA_PED_REG_RING_H
#define NATALIA_PED_REG_RING_H

#include <stdbool.h>
#include <stdint.h>

#define PED_REG_RING_CAPACITY        2048U
#define PED_REG_RING_SECOND_RESERVE  8U
#define PED_REG_RECORD_DATA_WORDS    5U

#define PED_REG_RECORD_EVENT         1U
#define PED_REG_RECORD_SECOND        2U

#define PED_REG_RECORD_FLAG_NO_COUNTERS 0x01U
#define PED_REG_RECORD_FLAG_NO_TIME     0x02U

typedef struct {
    uint8_t kind;
    uint8_t flags;
    uint16_t data[PED_REG_RECORD_DATA_WORDS];
    uint32_t rtc_seconds;
} PedRegRecord;

typedef struct {
    PedRegRecord records[PED_REG_RING_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t high_water;
    volatile uint32_t seconds_lost;
} PedRegRing;

void ped_reg_ring_reset(PedRegRing* ring);
uint32_t ped_reg_ring_count(const PedRegRing* ring);
bool ped_reg_ring_has_event_room(const PedRegRing* ring);
bool ped_reg_ring_push_event(PedRegRing* ring, const PedRegRecord* record);
bool ped_reg_ring_push_second(PedRegRing* ring, const PedRegRecord* record);
bool ped_reg_ring_pop(PedRegRing* ring, PedRegRecord* record);
uint32_t ped_reg_ring_pop_many(PedRegRing* ring, void* records, uint32_t max_records);
uint32_t ped_reg_ring_high_water(const PedRegRing* ring);
uint32_t ped_reg_ring_seconds_lost(const PedRegRing* ring);

#endif
