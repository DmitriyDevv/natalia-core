#ifndef NATALIA_CORE_TLM_STAGING_H
#define NATALIA_CORE_TLM_STAGING_H

#include <stdint.h>

#define TLM_STAGING_SLOTS 6U

uint8_t tlm_staging_put(const uint8_t* data, uint16_t length);
uint16_t tlm_staging_get(uint8_t slot, uint8_t* out, uint16_t capacity);

#endif /* NATALIA_CORE_TLM_STAGING_H */
