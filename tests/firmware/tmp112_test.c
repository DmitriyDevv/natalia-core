/*
 * TMP112 digital temperature sensor bring-up test (I2C).
 *
 * TMP112 sensors live on the TEMP I2C bus (I2C2). This test first scans the
 * TMP112 address range (0x48-0x4B) so the real ADD0-strapped addresses are
 * visible, then initializes the driver and reads the PU and PED sensors once
 * per second. Board_API maps PU->0x48 and PED->0x49 by default; if the scan
 * shows different addresses, adjust TMP112_CONFIG_PU/PED_ADDRESS_7BIT.
 *
 * Requires NATALIA_ENABLE_I2C_DRIVER=ON and NATALIA_ENABLE_TMP112_DRIVER=ON.
 * Output goes over the active debug log backend (e.g. NATALIA_LOG_BACKEND=CAN).
 */

#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "i2c.h"
#include "status.h"
#include "timebase.h"

#define TMP112_TEST_SCAN_FIRST 0x48U
#define TMP112_TEST_SCAN_LAST 0x4BU
#define TMP112_TEST_PERIOD_MS 1000U

static void log_kv(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void log_address_hex(uint8_t address) {
    static const char hex[] = "0123456789ABCDEF";
    char text[5];

    text[0] = '0';
    text[1] = 'x';
    text[2] = hex[(address >> 4U) & 0x0FU];
    text[3] = hex[address & 0x0FU];
    text[4] = '\0';

    debug_log_write(text);
}

static void log_temperature(const char *prefix, int32_t milli_c) {
    int32_t whole = milli_c / 1000;
    int32_t frac = milli_c % 1000;

    if (frac < 0) {
        frac = -frac;
    }

    debug_log_write(prefix);
    if ((milli_c < 0) && (whole == 0)) {
        debug_log_write("-");
    }
    {
        uint32_t whole_mag = (whole < 0) ? (uint32_t)(-whole) : (uint32_t)whole;
        if (whole < 0) {
            debug_log_write("-");
        }
        debug_log_write_u32_inline(whole_mag);
    }
    debug_log_write(".");
    if (frac < 100) {
        debug_log_write("0");
    }
    if (frac < 10) {
        debug_log_write("0");
    }
    debug_log_write_u32_inline((uint32_t)frac);
    debug_log_write("\r\n");
}

static void scan_temp_i2c_bus(void) {
    uint8_t address;
    BoardStatus status;

    debug_log_write("i2c temp scan start\r\n");

    status = i2c_init_bus_speed(I2C_BUS_TEMP, I2C_SPEED_100KHZ);
    log_kv("i2c_init_bus_temp", (uint32_t)status);
    if (status != BOARD_OK) {
        return;
    }

    for (address = TMP112_TEST_SCAN_FIRST; address <= TMP112_TEST_SCAN_LAST; ++address) {
        uint8_t is_present = 0U;

        status = i2c_probe(I2C_BUS_TEMP, address, &is_present);
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

    debug_log_write("i2c temp scan done\r\n");
}

static void read_sensor(const char *name, BoardTempSensorId sensor) {
    BoardDigitalTempSample sample;
    BoardStatus status;

    status = board_read_digital_temp(sensor, &sample);

    debug_log_write(name);
    debug_log_write(" status=");
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");

    if (status == BOARD_OK) {
        log_temperature("  temp_c=", sample.temperature_milli_c);
        log_kv("  raw_12bit", (uint32_t)(uint16_t)sample.raw_12bit);
        log_kv("  range_valid", (uint32_t)sample.range_valid);
    }
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\ntmp112 temp test start\r\n");

    status = board_init_hardware();
    log_kv("board_init_hardware", (uint32_t)status);

    scan_temp_i2c_bus();

    status = board_temp_digital_init();
    log_kv("board_temp_digital_init", (uint32_t)status);

    while (1) {
        read_sensor("PU", BOARD_TEMP_SENSOR_PU);
        read_sensor("PED", BOARD_TEMP_SENSOR_PED);
        debug_log_write("\r\n");

        timebase_delay_ms_blocking(TMP112_TEST_PERIOD_MS);
    }
}
