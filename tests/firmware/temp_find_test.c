#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "i2c.h"
#include "status.h"
#include "stm32l496xx.h"
#include "timebase.h"

volatile uint8_t g_i2c_scan_start_address = 0x48U;
volatile uint8_t g_i2c_scan_end_address = 0x4BU;
volatile uint8_t g_i2c_scan_speed = 0U;

static const char* status_to_string(BoardStatus status) {
    switch (status) {
    case BOARD_OK:
        return "BOARD_OK";

    case BOARD_ERR_INVALID_ARG:
        return "BOARD_ERR_INVALID_ARG";

    case BOARD_ERR_TIMEOUT:
        return "BOARD_ERR_TIMEOUT";

    case BOARD_ERR_IO:
        return "BOARD_ERR_IO";

    case BOARD_ERR_CRC:
        return "BOARD_ERR_CRC";

    case BOARD_ERR_BUSY:
        return "BOARD_ERR_BUSY";

    case BOARD_ERR_NOT_READY:
        return "BOARD_ERR_NOT_READY";

    case BOARD_ERR_UNSUPPORTED:
        return "BOARD_ERR_UNSUPPORTED";

    default:
        return "BOARD_ERR_UNKNOWN";
    }
}

static void log_status_line(const char* prefix, BoardStatus status) {
    debug_log_write(prefix);
    debug_log_write(status_to_string(status));
    debug_log_write("\n");
}

static void write_hex_nibble(uint8_t value) {
    value &= 0x0FU;

    if (value < 10U) {
        debug_log_write_u32_inline((uint32_t)value);
    } else {
        if (value == 10U) {
            debug_log_write("A");
        } else if (value == 11U) {
            debug_log_write("B");
        } else if (value == 12U) {
            debug_log_write("C");
        } else if (value == 13U) {
            debug_log_write("D");
        } else if (value == 14U) {
            debug_log_write("E");
        } else {
            debug_log_write("F");
        }
    }
}

static void write_hex_u8(uint8_t value) {
    debug_log_write("0x");
    write_hex_nibble((uint8_t)(value >> 4U));
    write_hex_nibble(value);
}

static void log_i2c_address_line(uint8_t address, BoardStatus status) {
    debug_log_write("addr=");
    write_hex_u8(address);
    debug_log_write(" status=");
    debug_log_write(status_to_string(status));
    debug_log_write("\n");
}

static I2cSpeed get_i2c_scan_speed(void) {
    if (g_i2c_scan_speed != 0U) {
        return I2C_SPEED_400KHZ;
    }

    return I2C_SPEED_100KHZ;
}

static uint8_t get_scan_start_address(void) {
    uint8_t address;

    address = g_i2c_scan_start_address;

    if (address < 0x08U) {
        address = 0x08U;
    }

    if (address > 0x77U) {
        address = 0x77U;
    }

    return address;
}

static uint8_t get_scan_end_address(void) {
    uint8_t address;

    address = g_i2c_scan_end_address;

    if (address < 0x08U) {
        address = 0x08U;
    }

    if (address > 0x77U) {
        address = 0x77U;
    }

    return address;
}

static BoardStatus read_tmp112_temperature_register(uint8_t address, uint8_t* msb, uint8_t* lsb) {
    uint8_t reg;
    uint8_t rx[2];

    if ((msb == 0) || (lsb == 0)) {
        return BOARD_ERR_INVALID_ARG;
    }

    reg = 0x00U;
    rx[0] = 0U;
    rx[1] = 0U;

    *msb = 0U;
    *lsb = 0U;

    if (i2c_write_read(I2C_BUS_TEMP, address, &reg, 1U, rx, sizeof(rx)) != BOARD_OK) {
        return i2c_write_read(I2C_BUS_TEMP, address, &reg, 1U, rx, sizeof(rx));
    }

    *msb = rx[0];
    *lsb = rx[1];

    return BOARD_OK;
}

static void scan_tmp112_range(void) {
    uint8_t start_address;
    uint8_t end_address;
    uint8_t address;
    uint8_t msb;
    uint8_t lsb;
    uint32_t found_count;
    BoardStatus status;

    start_address = get_scan_start_address();
    end_address = get_scan_end_address();

    if (start_address > end_address) {
        address = start_address;
        start_address = end_address;
        end_address = address;
    }

    found_count = 0U;

    debug_log_write("scan_start=");
    write_hex_u8(start_address);
    debug_log_write(" scan_end=");
    write_hex_u8(end_address);
    debug_log_write("\n");

    address = start_address;

    while (address <= end_address) {
        msb = 0U;
        lsb = 0U;

        status = read_tmp112_temperature_register(address, &msb, &lsb);
        if (status == BOARD_OK) {
            ++found_count;

            debug_log_write("found addr=");
            write_hex_u8(address);
            debug_log_write(" temp_reg=");
            write_hex_u8(msb);
            debug_log_write(" ");
            write_hex_u8(lsb);
            debug_log_write("\n");
        } else if (status == BOARD_ERR_NOT_READY) {
            debug_log_write("absent addr=");
            write_hex_u8(address);
            debug_log_write("\n");
        } else {
            log_i2c_address_line(address, status);
        }

        if (address == 0x77U) {
            break;
        }

        ++address;
    }

    debug_log_write("scan_done found=");
    debug_log_write_u32_inline(found_count);
    debug_log_write("\n");
}

int main(void) {
    BoardStatus status;
    I2cSpeed speed;

    status = clock_init();
    if (status != BOARD_OK) {
        return 1;
    }

    status = timebase_init();
    if (status != BOARD_OK) {
        return 1;
    }

    status = debug_log_init();
    if (status != BOARD_OK) {
        return 1;
    }

    debug_log_write("\n");
    debug_log_write("i2c temp scan test start\n");

    status = board_init_hardware();
    log_status_line("board_init_hardware=", status);
    if (status != BOARD_OK) {
        while (1) {
            __WFI();
        }
    }

    speed = get_i2c_scan_speed();

    if (speed == I2C_SPEED_400KHZ) {
        debug_log_write("i2c_speed=400kHz\n");
    } else {
        debug_log_write("i2c_speed=100kHz\n");
    }

    status = i2c_init_bus_speed(I2C_BUS_TEMP, speed);
    log_status_line("i2c_init_bus_speed=", status);
    if (status != BOARD_OK) {
        while (1) {
            __WFI();
        }
    }

    scan_tmp112_range();

    while (1) {
        __WFI();
    }
}