#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "status.h"
#include "timebase.h"

#define TEST_PACKET_SIZE 2048U
#define TEST_HEADER_SIZE 64U
#define TEST_PAYLOAD_SIZE (TEST_PACKET_SIZE - TEST_HEADER_SIZE)
#define TEST_PACKET_COUNT 32768U
#define TEST_RUN_SEED 0x4E415441UL

static uint8_t packet[TEST_PACKET_SIZE];

static void panic_loop(void) {
    while (1) {
        __asm volatile ("nop");
    }
}

static void wait_ok(BoardStatus status) {
    if (status != BOARD_OK) {
        panic_loop();
    }
}

static void write_u32_le(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint32_t crc32_update(uint32_t crc, uint8_t value) {
    crc ^= value;

    for (uint32_t i = 0U; i < 8U; ++i) {
        if ((crc & 1U) != 0U) {
            crc = (crc >> 1U) ^ 0xEDB88320UL;
        } else {
            crc >>= 1U;
        }
    }

    return crc;
}

static uint32_t crc32_compute(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t i = 0U; i < size; ++i) {
        crc = crc32_update(crc, data[i]);
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t pattern_byte(uint32_t packet_index, uint32_t offset) {
    uint32_t value;

    value = TEST_RUN_SEED;
    value ^= packet_index * 0x9E3779B1UL;
    value ^= offset * 0x85EBCA6BUL;
    value ^= value >> 16U;
    value *= 0x7FEB352DUL;
    value ^= value >> 15U;
    value *= 0x846CA68BUL;
    value ^= value >> 16U;

    return (uint8_t)(value & 0xFFU);
}

static void build_packet(uint32_t packet_index) {
    uint32_t payload_crc;
    uint32_t frame_crc;

    for (uint32_t i = 0U; i < TEST_PACKET_SIZE; ++i) {
        packet[i] = 0U;
    }

    packet[0] = 'N';
    packet[1] = 'T';
    packet[2] = 'L';
    packet[3] = 'T';

    write_u32_le(&packet[4], packet_index);
    write_u32_le(&packet[8], ~packet_index);
    write_u32_le(&packet[12], TEST_PACKET_SIZE);
    write_u32_le(&packet[16], TEST_HEADER_SIZE);
    write_u32_le(&packet[20], TEST_PAYLOAD_SIZE);
    write_u32_le(&packet[32], TEST_RUN_SEED);
    write_u32_le(&packet[36], TEST_PACKET_COUNT);
    write_u32_le(&packet[40], timebase_millis());
    write_u32_le(&packet[44], 0xA55A5AA5UL);
    write_u32_le(&packet[48], 0x12345678UL);
    write_u32_le(&packet[52], 0x87654321UL);
    write_u32_le(&packet[56], 0x55AA55AAUL);
    write_u32_le(&packet[60], 0xAA55AA55UL);

    for (uint32_t i = 0U; i < TEST_PAYLOAD_SIZE; ++i) {
        packet[TEST_HEADER_SIZE + i] = pattern_byte(packet_index, i);
    }

    payload_crc = crc32_compute(&packet[TEST_HEADER_SIZE], TEST_PAYLOAD_SIZE);
    write_u32_le(&packet[24], payload_crc);

    write_u32_le(&packet[28], 0U);
    frame_crc = crc32_compute(packet, TEST_PACKET_SIZE);
    write_u32_le(&packet[28], frame_crc);
}

int main(void) {
    uint8_t ready = 0U;

    wait_ok(clock_init());
    wait_ok(timebase_init());
    wait_ok(board_init_hardware());

    while (ready == 0U) {
        wait_ok(board_usb_is_ready(&ready));
    }

    timebase_delay_ms_blocking(5000U);

    for (uint32_t packet_index = 0U; packet_index < TEST_PACKET_COUNT; ++packet_index) {
        size_t written = 0U;

        build_packet(packet_index);

        wait_ok(board_usb_write(packet, TEST_PACKET_SIZE, &written));

        if (written != TEST_PACKET_SIZE) {
            panic_loop();
        }
    }

    while (1) {
        __asm volatile ("nop");
    }
}
