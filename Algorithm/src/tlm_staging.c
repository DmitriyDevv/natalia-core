#include "tlm_staging.h"

#include <string.h>

#include "state.h"

static struct {
    uint8_t data[TLM_PAYLOAD_MAX];
    uint16_t length;
} tlm_slots[TLM_STAGING_SLOTS];

static uint8_t tlm_next_slot;

uint8_t tlm_staging_put(const uint8_t* data, uint16_t length) {
    uint8_t slot = tlm_next_slot;

    tlm_next_slot = (uint8_t)((tlm_next_slot + 1U) % TLM_STAGING_SLOTS);

    if (length > (uint16_t)TLM_PAYLOAD_MAX) {
        length = (uint16_t)TLM_PAYLOAD_MAX;
    }

    if ((data != NULL) && (length > 0U)) {
        (void)memcpy(tlm_slots[slot].data, data, length);
    }

    tlm_slots[slot].length = length;

    return slot;
}

uint16_t tlm_staging_get(uint8_t slot, uint8_t* out, uint16_t capacity) {
    uint16_t length;

    if (slot >= (uint8_t)TLM_STAGING_SLOTS) {
        return 0U;
    }

    length = tlm_slots[slot].length;

    if (length > capacity) {
        length = capacity;
    }

    if ((out != NULL) && (length > 0U)) {
        (void)memcpy(out, tlm_slots[slot].data, length);
    }

    return length;
}
