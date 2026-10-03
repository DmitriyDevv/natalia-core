#include "ped_reg_ring.h"

#include <stdatomic.h>
#include <stddef.h>

_Static_assert((PED_REG_RING_CAPACITY & (PED_REG_RING_CAPACITY - 1U)) == 0U,
               "PED ring capacity must be a power of two");
_Static_assert(PED_REG_RING_SECOND_RESERVE < PED_REG_RING_CAPACITY,
               "PED ring reserve must be smaller than the ring");

#define PED_REG_RING_MASK (PED_REG_RING_CAPACITY - 1U)

static uint32_t ped_reg_ring_free(const PedRegRing* ring) {
    return PED_REG_RING_CAPACITY - (ring->tail - ring->head);
}

static void ped_reg_ring_store(PedRegRing* ring, const PedRegRecord* record) {
    uint32_t tail = ring->tail;
    uint32_t count;

    ring->records[tail & PED_REG_RING_MASK] = *record;
    atomic_signal_fence(memory_order_seq_cst);
    ring->tail = tail + 1U;

    count = ring->tail - ring->head;
    if (count > ring->high_water) {
        ring->high_water = count;
    }
}

void ped_reg_ring_reset(PedRegRing* ring) {
    if (ring == NULL) {
        return;
    }

    ring->head = 0U;
    ring->tail = 0U;
    ring->high_water = 0U;
    ring->seconds_lost = 0U;
}

uint32_t ped_reg_ring_count(const PedRegRing* ring) {
    return (ring == NULL) ? 0U : (ring->tail - ring->head);
}

bool ped_reg_ring_has_event_room(const PedRegRing* ring) {
    return (ring != NULL) && (ped_reg_ring_free(ring) > PED_REG_RING_SECOND_RESERVE);
}

bool ped_reg_ring_push_event(PedRegRing* ring, const PedRegRecord* record) {
    if ((record == NULL) || !ped_reg_ring_has_event_room(ring)) {
        return false;
    }

    ped_reg_ring_store(ring, record);

    return true;
}

bool ped_reg_ring_push_second(PedRegRing* ring, const PedRegRecord* record) {
    if ((ring == NULL) || (record == NULL)) {
        return false;
    }

    if (ped_reg_ring_free(ring) == 0U) {
        ++ring->seconds_lost;
        return false;
    }

    ped_reg_ring_store(ring, record);

    return true;
}

bool ped_reg_ring_pop(PedRegRing* ring, PedRegRecord* record) {
    uint32_t head;

    if ((ring == NULL) || (record == NULL)) {
        return false;
    }

    head = ring->head;
    if (head == ring->tail) {
        return false;
    }

    atomic_signal_fence(memory_order_seq_cst);
    *record = ring->records[head & PED_REG_RING_MASK];
    atomic_signal_fence(memory_order_seq_cst);
    ring->head = head + 1U;

    return true;
}

uint32_t ped_reg_ring_high_water(const PedRegRing* ring) {
    return (ring == NULL) ? 0U : ring->high_water;
}

uint32_t ped_reg_ring_seconds_lost(const PedRegRing* ring) {
    return (ring == NULL) ? 0U : ring->seconds_lost;
}
