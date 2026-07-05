#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "status.h"
#include "timebase.h"

#define PACKET_SIZE 2048U
#define PACKET_COUNT 16U

static uint8_t packet[PACKET_SIZE];

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

static uint8_t pattern_byte(uint32_t packet_index, uint32_t offset) {
    uint32_t value;

    value = 0x4E415441UL;
    value ^= packet_index * 0x01010101UL;
    value ^= offset * 0x0001003DUL;
    value ^= value >> 16U;
    value ^= value >> 8U;

    return (uint8_t)(value & 0xFFU);
}

static void fill_packet(uint32_t packet_index) {
    uint32_t i;

    for (i = 0U; i < PACKET_SIZE; ++i) {
        packet[i] = pattern_byte(packet_index, i);
    }
}

int main(void) {
    uint8_t ready = 0U;
    uint32_t packet_index;
    size_t written;

    wait_ok(clock_init());
    wait_ok(timebase_init());
    wait_ok(board_init_hardware());

    while (ready == 0U) {
        wait_ok(board_usb_is_ready(&ready));
    }

    timebase_delay_ms_blocking(10000U);

    for (packet_index = 0U; packet_index < PACKET_COUNT; ++packet_index) {
        fill_packet(packet_index);

        written = 0U;
        wait_ok(board_usb_write(packet, PACKET_SIZE, &written));

        if (written != PACKET_SIZE) {
            panic_loop();
        }

        timebase_delay_ms_blocking(10U);
    }

    while (1) {
        __asm volatile ("nop");
    }
}
