#include "mram.h"

#include <stddef.h>
#include <stdint.h>

#include "spi.h"

#define MRAM_CMD_WREN 0x06U
#define MRAM_CMD_WRDI 0x04U
#define MRAM_CMD_RDSR 0x05U
#define MRAM_CMD_READ 0x03U
#define MRAM_CMD_WRITE 0x02U

#define MRAM_STATUS_WEL 0x02U

#define MRAM_HEADER_SIZE 4U

static BoardStatus mram_bank_to_bus(MramBank bank, SpiBusId* bus) {
    switch (bank) {
    case MRAM_BANK_1:
        *bus = SPI_BUS_MRAM1;
        return BOARD_OK;
    case MRAM_BANK_2:
        *bus = SPI_BUS_MRAM2;
        return BOARD_OK;
    default:
        return BOARD_ERR_INVALID_ARG;
    }
}

static BoardStatus mram_simple_command(SpiBusId bus, uint8_t command) {
    BoardStatus status;

    status = spi_cs_assert(bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_transfer(bus, &command, 0, 1U);
    if (status != BOARD_OK) {
        (void)spi_cs_release(bus);
        return status;
    }

    return spi_cs_release(bus);
}

static void mram_fill_header(uint8_t* header, uint8_t command, uint32_t address) {
    header[0] = command;
    header[1] = (uint8_t)((address >> 16) & 0xFFU);
    header[2] = (uint8_t)((address >> 8) & 0xFFU);
    header[3] = (uint8_t)(address & 0xFFU);
}

static BoardStatus mram_validate_range(uint32_t address, size_t size) {
    if (size == 0U) {
        return BOARD_OK;
    }

    if (address >= MRAM_CAPACITY_BYTES) {
        return BOARD_ERR_INVALID_ARG;
    }

    if (size > (size_t)(MRAM_CAPACITY_BYTES - address)) {
        return BOARD_ERR_INVALID_ARG;
    }

    return BOARD_OK;
}

BoardStatus mram_init(MramBank bank) {
    SpiBusId bus;
    BoardStatus status;

    status = mram_bank_to_bus(bank, &bus);
    if (status != BOARD_OK) {
        return status;
    }

    return spi_init_bus(bus);
}

BoardStatus mram_read_status(MramBank bank, uint8_t* status_reg) {
    SpiBusId bus;
    BoardStatus status;
    uint8_t command = MRAM_CMD_RDSR;

    if (status_reg == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = mram_bank_to_bus(bank, &bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_cs_assert(bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_transfer(bus, &command, 0, 1U);
    if (status == BOARD_OK) {
        status = spi_transfer(bus, 0, status_reg, 1U);
    }

    if (status != BOARD_OK) {
        (void)spi_cs_release(bus);
        return status;
    }

    return spi_cs_release(bus);
}

BoardStatus mram_probe(MramBank bank, uint8_t* is_alive) {
    BoardStatus status;
    uint8_t status_after_wren = 0U;
    uint8_t status_after_wrdi = 0U;
    SpiBusId bus;

    if (is_alive == 0) {
        return BOARD_ERR_INVALID_ARG;
    }

    *is_alive = 0U;

    status = mram_bank_to_bus(bank, &bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_simple_command(bus, MRAM_CMD_WREN);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read_status(bank, &status_after_wren);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_simple_command(bus, MRAM_CMD_WRDI);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_read_status(bank, &status_after_wrdi);
    if (status != BOARD_OK) {
        return status;
    }

    if (((status_after_wren & MRAM_STATUS_WEL) != 0U) &&
        ((status_after_wrdi & MRAM_STATUS_WEL) == 0U)) {
        *is_alive = 1U;
    }

    return BOARD_OK;
}

BoardStatus mram_read(MramBank bank, uint32_t address, void* buffer, size_t size) {
    SpiBusId bus;
    BoardStatus status;
    uint8_t header[MRAM_HEADER_SIZE];

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = mram_validate_range(address, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_bank_to_bus(bank, &bus);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    mram_fill_header(header, MRAM_CMD_READ, address);

    status = spi_cs_assert(bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_transfer(bus, header, 0, MRAM_HEADER_SIZE);
    if (status == BOARD_OK) {
        status = spi_transfer(bus, 0, (uint8_t*)buffer, size);
    }

    if (status != BOARD_OK) {
        (void)spi_cs_release(bus);
        return status;
    }

    return spi_cs_release(bus);
}

BoardStatus mram_write(MramBank bank, uint32_t address, const void* buffer, size_t size) {
    SpiBusId bus;
    BoardStatus status;
    uint8_t header[MRAM_HEADER_SIZE];

    if ((buffer == 0) && (size > 0U)) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = mram_validate_range(address, size);
    if (status != BOARD_OK) {
        return status;
    }

    status = mram_bank_to_bus(bank, &bus);
    if (status != BOARD_OK) {
        return status;
    }

    if (size == 0U) {
        return BOARD_OK;
    }

    status = mram_simple_command(bus, MRAM_CMD_WREN);
    if (status != BOARD_OK) {
        return status;
    }

    mram_fill_header(header, MRAM_CMD_WRITE, address);

    status = spi_cs_assert(bus);
    if (status != BOARD_OK) {
        return status;
    }

    status = spi_transfer(bus, header, 0, MRAM_HEADER_SIZE);
    if (status == BOARD_OK) {
        status = spi_transfer(bus, (const uint8_t*)buffer, 0, size);
    }

    if (status != BOARD_OK) {
        (void)spi_cs_release(bus);
        return status;
    }

    return spi_cs_release(bus);
}
