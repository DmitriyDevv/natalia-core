#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "i2c.h"
#include "status.h"
#include "timebase.h"

static void log_status(const char* label, BoardStatus status) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");
}

static void log_i32_inline(int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        debug_log_write("-");
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    debug_log_write_u32_inline(magnitude);
}

static void log_address_hex(uint8_t address) {
    static const char hex[] = "0123456789ABCDEF";

    debug_log_write("0x");
    debug_log_write((char[]){hex[(address >> 4U) & 0x0FU], hex[address & 0x0FU], 0});
}

static void scan_power_i2c_bus(void) {
    uint8_t address;
    uint8_t is_present;
    BoardStatus status;

    debug_log_write("i2c power scan start\r\n");

    status = i2c_init_bus_speed(I2C_BUS_POWER, I2C_SPEED_100KHZ);
    log_status("i2c_init_bus_power", status);

    if (status != BOARD_OK) {
        return;
    }

    for (address = 0x08U; address <= 0x77U; ++address) {
        is_present = 0U;

        status = i2c_probe(I2C_BUS_POWER, address, &is_present);
        if (status != BOARD_OK) {
            debug_log_write("i2c_probe_error addr=");
            log_address_hex(address);
            debug_log_write(" status=");
            debug_log_write_u32_inline((uint32_t)status);
            debug_log_write("\r\n");
        } else if (is_present != 0U) {
            debug_log_write("i2c_found addr=");
            log_address_hex(address);
            debug_log_write("\r\n");
        }
    }

    debug_log_write("i2c power scan done\r\n");
}

static void log_power_sample(const char* name, BoardPowerMonitorId monitor) {
    BoardPowerSample sample;
    BoardStatus status;

    status = board_read_power_monitor(monitor, &sample);

    debug_log_write(name);
    debug_log_write(" status=");
    debug_log_write_u32_inline((uint32_t)status);

    if (status == BOARD_OK) {
        debug_log_write(" ready=");
        debug_log_write_u32_inline((uint32_t)sample.ready);

        debug_log_write(" bus_mv=");
        debug_log_write_u32_inline(sample.bus_voltage_mv);

        debug_log_write(" shunt_uv=");
        log_i32_inline(sample.shunt_voltage_uv);

        debug_log_write(" current_ua=");
        log_i32_inline(sample.current_ua);

        debug_log_write(" power_uw=");
        debug_log_write_u32_inline(sample.power_uw);

        debug_log_write(" cnvr=");
        debug_log_write_u32_inline((uint32_t)sample.conversion_ready);

        debug_log_write(" ovf=");
        debug_log_write_u32_inline((uint32_t)sample.math_overflow);
    }

    debug_log_write("\r\n");
}

int main(void) {
    BoardStatus status;

    clock_init();
    timebase_init();

    status = debug_log_init();

    if (status == BOARD_OK) {
        debug_log_write("\r\nina219 test start\r\n");
    }

    status = board_init_hardware();
    log_status("board_init_hardware", status);

    scan_power_i2c_bus();

    status = board_power_monitor_init();
    log_status("board_power_monitor_init", status);

    while (1) {
        log_power_sample("PU", BOARD_POWER_MONITOR_PU);
        log_power_sample("PED", BOARD_POWER_MONITOR_PED);
        debug_log_write("\r\n");

        timebase_delay_ms_blocking(1000U);
    }
}
