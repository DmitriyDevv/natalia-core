#ifndef NATALIA_SPI_H
#define NATALIA_SPI_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

typedef enum {
    SPI_BUS_MRAM1 = 0,
    SPI_BUS_MRAM2
} SpiBusId;

BoardStatus spi_init_bus(SpiBusId bus);
BoardStatus spi_deinit_bus(SpiBusId bus);

BoardStatus spi_cs_assert(SpiBusId bus);
BoardStatus spi_cs_release(SpiBusId bus);

BoardStatus spi_transfer(SpiBusId bus, const uint8_t* tx, uint8_t* rx, size_t size);

#endif /* NATALIA_SPI_H */
