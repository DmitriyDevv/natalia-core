#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "crc16.h"
#include "debug_log.h"
#include "timebase.h"

#define CRC_TEST_BUFFER_BYTES     4100U
#define CRC_TEST_MAX_LENGTH       4096U
#define CRC_TEST_RANDOM_ROUNDS    5000U
#define CRC_TEST_SPEED_ROUNDS     500U
#define CRC_TEST_SPEED_BYTES      2048U
#define CRC_TEST_CHECK_VALUE      0x29B1U

static uint8_t crc_test_buffer[CRC_TEST_BUFFER_BYTES];
static uint32_t crc_test_seed = 0x12345678UL;

static uint32_t crc_test_random(void) {
    crc_test_seed ^= crc_test_seed << 13;
    crc_test_seed ^= crc_test_seed >> 17;
    crc_test_seed ^= crc_test_seed << 5;

    return crc_test_seed;
}

static void crc_test_log_u32(const char *label, uint32_t value) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
    debug_log_write("\r\n");
}

static void crc_test_halt(void) {
    while (1) {
        __asm volatile("nop");
    }
}

static uint32_t crc_test_check_vector(void) {
    static const uint8_t vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t hardware = crc16_ccitt(vector, sizeof(vector));
    uint16_t software = crc16_ccitt_software(vector, sizeof(vector));

    crc_test_log_u32("vector_hw", hardware);
    crc_test_log_u32("vector_sw", software);

    return ((hardware == CRC_TEST_CHECK_VALUE) && (software == CRC_TEST_CHECK_VALUE)) ? 0U : 1U;
}

static uint32_t crc_test_random_buffers(void) {
    uint32_t round;
    uint32_t mismatches = 0U;
    size_t i;

    for (round = 0U; round < CRC_TEST_RANDOM_ROUNDS; ++round) {
        size_t length = (size_t)(crc_test_random() % (CRC_TEST_MAX_LENGTH + 1U));
        size_t offset = (size_t)(crc_test_random() % 4U);
        uint16_t hardware;
        uint16_t software;

        for (i = 0U; i < (offset + length); ++i) {
            crc_test_buffer[i] = (uint8_t)(crc_test_random() & 0xFFU);
        }

        hardware = crc16_ccitt(&crc_test_buffer[offset], length);
        software = crc16_ccitt_software(&crc_test_buffer[offset], length);

        if (hardware != software) {
            if (mismatches == 0U) {
                crc_test_log_u32("first_mismatch_round", round);
                crc_test_log_u32("length", (uint32_t)length);
                crc_test_log_u32("offset", (uint32_t)offset);
                crc_test_log_u32("hw", hardware);
                crc_test_log_u32("sw", software);
            }
            ++mismatches;
        }
    }

    crc_test_log_u32("random_rounds", CRC_TEST_RANDOM_ROUNDS);
    crc_test_log_u32("mismatches", mismatches);

    return mismatches;
}

static void crc_test_speed(void) {
    volatile uint16_t sink = 0U;
    uint32_t start_ms;
    uint32_t round;

    start_ms = timebase_millis();
    for (round = 0U; round < CRC_TEST_SPEED_ROUNDS; ++round) {
        sink = (uint16_t)(sink ^ crc16_ccitt(crc_test_buffer, CRC_TEST_SPEED_BYTES));
    }
    crc_test_log_u32("hw_ms_per_500x2048", timebase_millis() - start_ms);

    start_ms = timebase_millis();
    for (round = 0U; round < CRC_TEST_SPEED_ROUNDS; ++round) {
        sink = (uint16_t)(sink ^ crc16_ccitt_software(crc_test_buffer, CRC_TEST_SPEED_BYTES));
    }
    crc_test_log_u32("sw_ms_per_500x2048", timebase_millis() - start_ms);

    (void)sink;
}

int main(void) {
    uint32_t failures = 0U;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    debug_log_write("\r\nCRC16 HW TEST\r\n");

    failures += crc_test_check_vector();
    failures += crc_test_random_buffers();
    crc_test_speed();

    debug_log_write((failures == 0U) ? "CRC16 HW TEST PASS\r\n" : "CRC16 HW TEST FAIL\r\n");

    crc_test_halt();

    return 0;
}
