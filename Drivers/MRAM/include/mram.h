#ifndef NATALIA_MRAM_H
#define NATALIA_MRAM_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

#define MRAM_CAPACITY_BYTES (512UL * 1024UL)

typedef enum {
    MRAM_BANK_1 = 1,
    MRAM_BANK_2 = 2
} MramBank;

BoardStatus mram_init(MramBank bank);
BoardStatus mram_probe(MramBank bank, uint8_t* is_alive);
BoardStatus mram_read_status(MramBank bank, uint8_t* status_reg);
BoardStatus mram_read(MramBank bank, uint32_t address, void* buffer, size_t size);
BoardStatus mram_write(MramBank bank, uint32_t address, const void* buffer, size_t size);

#endif /* NATALIA_MRAM_H */
