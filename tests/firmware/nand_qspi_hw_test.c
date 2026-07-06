#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "qspi.h"
#include "status.h"
#include "timebase.h"

#define TEST_BANK_ID 2U
#define TEST_PACKET_SIZE 2048U
#define TEST_HEADER_SIZE 64U
#define TEST_PACKET_COUNT 32768U

#define TEST_MODE_USB_ONLY 1U
#define TEST_MODE_NAND_READ_ONLY 2U
#define TEST_MODE_NAND_USB_DUMP 3U
#define TEST_MODE_NAND_WRITE_PREPARE 4U

#ifndef TEST_MODE
#define TEST_MODE TEST_MODE_NAND_USB_DUMP
#endif

static uint8_t packet[TEST_PACKET_SIZE];
static char text_buffer[256];

static void panic_loop(void) {
    static const char panic_text[] = "PANIC!!!!\n";
    size_t written = 0U;
    uint8_t ready = 0U;
    uint32_t timeout;

    timeout = 1000000U;

    while (timeout > 0U) {
        if (board_usb_is_ready(&ready) == BOARD_OK) {
            if (ready != 0U) {
                break;
            }
        }

        --timeout;
    }

    if (ready != 0U) {
        (void)board_usb_write(panic_text, sizeof(panic_text) - 1U, &written);
    }

    while (1) {
        __asm volatile ("nop");
    }
}


static void wait_ok(BoardStatus status) {
    if (status != BOARD_OK) {
        panic_loop();
    }
}

static void wait_usb_ready(void) {
    uint8_t ready = 0U;

    while (ready == 0U) {
        wait_ok(board_usb_is_ready(&ready));
    }
}

static void wait_write_idle(void) {
    uint8_t is_idle = 0U;

    while (is_idle == 0U) {
        wait_ok(board_nand_write_poll(TEST_BANK_ID, &is_idle));
    }
}

static void wait_write_flush(void) {
    uint8_t is_done = 0U;

    while (is_done == 0U) {
        wait_ok(board_nand_write_flush(TEST_BANK_ID, &is_done));
    }
}

static void wait_erase_done(void) {
    uint8_t is_done = 0U;

    while (is_done == 0U) {
        wait_ok(board_nand_erase_is_done(TEST_BANK_ID, &is_done));
    }
}

static void write_u32_le(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFU);
    dst[1] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[2] = (uint8_t)((value >> 16U) & 0xFFU);
    dst[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint8_t payload_byte(uint32_t offset) {
    uint32_t value;

    value = 0x4E415441UL;
    value ^= offset * 0x9E3779B1UL;
    value ^= value >> 16U;
    value *= 0x7FEB352DUL;
    value ^= value >> 15U;
    value *= 0x846CA68BUL;
    value ^= value >> 16U;

    return (uint8_t)(value & 0xFFU);
}

static void build_packet_template(void) {
    uint32_t index;

    for (index = 0U; index < TEST_PACKET_SIZE; ++index) {
        packet[index] = payload_byte(index);
    }
}

static void stamp_packet(uint32_t packet_index) {
    packet[0] = 'N';
    packet[1] = 'T';
    packet[2] = 'L';
    packet[3] = 'U';

    write_u32_le(&packet[4], packet_index);
    write_u32_le(&packet[8], ~packet_index);
    write_u32_le(&packet[12], TEST_PACKET_SIZE);
    write_u32_le(&packet[16], TEST_PACKET_COUNT);
    write_u32_le(&packet[20], TEST_HEADER_SIZE);
    write_u32_le(&packet[24], 0xA55A5AA5UL);
    write_u32_le(&packet[28], 0x55AA55AAUL);
    write_u32_le(&packet[32], 0x12345678UL);
    write_u32_le(&packet[36], 0x87654321UL);
    write_u32_le(&packet[40], 0x4E415441UL);
    write_u32_le(&packet[44], 0x55534230UL);
    write_u32_le(&packet[48], packet_index ^ 0x13579BDFUL);
    write_u32_le(&packet[52], packet_index + 0x2468ACE0UL);
    write_u32_le(&packet[56], 0xCAFEBABEUL);
    write_u32_le(&packet[60], 0xDEADBEEFUL);
}

static void send_packet(void) {
    size_t written = 0U;

    wait_ok(board_usb_write(packet, TEST_PACKET_SIZE, &written));

    if (written != TEST_PACKET_SIZE) {
        panic_loop();
    }
}

static uint32_t checksum_update_packet(uint32_t checksum) {
    checksum ^= packet[0];
    checksum *= 16777619UL;
    checksum ^= packet[1];
    checksum *= 16777619UL;
    checksum ^= packet[4];
    checksum *= 16777619UL;
    checksum ^= packet[64];
    checksum *= 16777619UL;
    checksum ^= packet[257];
    checksum *= 16777619UL;
    checksum ^= packet[1024];
    checksum *= 16777619UL;
    checksum ^= packet[2047];
    checksum *= 16777619UL;

    return checksum;
}

static size_t append_char(size_t pos, char value) {
    if (pos < (sizeof(text_buffer) - 1U)) {
        text_buffer[pos] = value;
        ++pos;
        text_buffer[pos] = '\0';
    }

    return pos;
}

static size_t append_string(size_t pos, const char* value) {
    while (*value != '\0') {
        pos = append_char(pos, *value);
        ++value;
    }

    return pos;
}

static size_t append_u32_dec(size_t pos, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;

    if (value == 0U) {
        return append_char(pos, '0');
    }

    while (value != 0U) {
        digits[count] = (char)('0' + (value % 10U));
        value /= 10U;
        ++count;
    }

    while (count != 0U) {
        --count;
        pos = append_char(pos, digits[count]);
    }

    return pos;
}

static size_t append_u64_dec(size_t pos, uint64_t value) {
    char digits[20];
    uint32_t count = 0U;

    if (value == 0U) {
        return append_char(pos, '0');
    }

    while (value != 0U) {
        digits[count] = (char)('0' + (uint8_t)(value % 10ULL));
        value /= 10ULL;
        ++count;
    }

    while (count != 0U) {
        --count;
        pos = append_char(pos, digits[count]);
    }

    return pos;
}

static size_t append_u32_hex(size_t pos, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t shift;

    pos = append_string(pos, "0x");

    for (shift = 28U; shift <= 28U; shift -= 4U) {
        pos = append_char(pos, hex[(value >> shift) & 0x0FU]);

        if (shift == 0U) {
            break;
        }
    }

    return pos;
}

static void debug_send_write_line(const char* stage,
                                  uint32_t packet_index,
                                  BoardStatus status) {
    size_t pos = 0U;
    size_t written = 0U;
    uint8_t ready = 0U;

    if (board_usb_is_ready(&ready) != BOARD_OK) {
        return;
    }

    if (ready == 0U) {
        return;
    }

    pos = append_string(pos, "WRITE ");
    pos = append_string(pos, stage);
    pos = append_string(pos, " packet=");
    pos = append_u32_dec(pos, packet_index);
    pos = append_string(pos, " status=");
    pos = append_u32_dec(pos, (uint32_t)status);
    pos = append_char(pos, '\n');

    (void)board_usb_write(text_buffer, pos, &written);
}

static void debug_send_snapshot(const char* stage, uint32_t packet_index) {
    QspiDebugSnapshot snapshot;
    size_t pos = 0U;
    size_t written = 0U;
    uint8_t ready = 0U;

    if (qspi_debug_snapshot(&snapshot) != BOARD_OK) {
        return;
    }

    if (board_usb_is_ready(&ready) != BOARD_OK) {
        return;
    }

    if (ready == 0U) {
        return;
    }

    pos = append_string(pos, "SNAP ");
    pos = append_string(pos, stage);
    pos = append_string(pos, " packet=");
    pos = append_u32_dec(pos, packet_index);
    pos = append_string(pos, " busy=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_busy);
    pos = append_string(pos, " tcf=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tcf);
    pos = append_string(pos, " ftf=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_ftf);
    pos = append_string(pos, " tef=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tef);
    pos = append_string(pos, " tof=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tof);
    pos = append_string(pos, " flvl=");
    pos = append_u32_dec(pos, snapshot.qspi_flevel);
    pos = append_string(pos, " dmaen=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_dmaen);
    pos = append_string(pos, " tcie=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_tcie);
    pos = append_string(pos, " teie=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_teie);
    pos = append_string(pos, " dma_en=");
    pos = append_u32_dec(pos, snapshot.dma_ccr_en);
    pos = append_string(pos, " dma_tc=");
    pos = append_u32_dec(pos, snapshot.dma_isr_tcif);
    pos = append_string(pos, " dma_te=");
    pos = append_u32_dec(pos, snapshot.dma_isr_teif);
    pos = append_string(pos, " ccr=");
    pos = append_u32_hex(pos, snapshot.qspi_ccr);
    pos = append_string(pos, " ar=");
    pos = append_u32_hex(pos, snapshot.qspi_ar);
    pos = append_string(pos, " dlr=");
    pos = append_u32_dec(pos, snapshot.qspi_dlr);
    pos = append_string(pos, " dcndtr=");
    pos = append_u32_dec(pos, snapshot.dma_cndtr);
    pos = append_string(pos, " dstate=");
    pos = append_u32_dec(pos, snapshot.dma_state);
    pos = append_string(pos, " dstatus=");
    pos = append_u32_dec(pos, snapshot.dma_status);
    pos = append_char(pos, '\n');

    (void)board_usb_write(text_buffer, pos, &written);
}

static void debug_send_timeout_snapshot(const char* stage, uint32_t packet_index) {
    QspiDebugSnapshot snapshot;
    uint8_t is_valid = 0U;
    size_t pos = 0U;
    size_t written = 0U;
    uint8_t ready = 0U;

    if (qspi_debug_last_dma_timeout(&snapshot, &is_valid) != BOARD_OK) {
        return;
    }

    if (board_usb_is_ready(&ready) != BOARD_OK) {
        return;
    }

    if (ready == 0U) {
        return;
    }

    pos = append_string(pos, "SNAPT ");
    pos = append_string(pos, stage);
    pos = append_string(pos, " packet=");
    pos = append_u32_dec(pos, packet_index);
    pos = append_string(pos, " valid=");
    pos = append_u32_dec(pos, is_valid);
    pos = append_string(pos, " busy=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_busy);
    pos = append_string(pos, " tcf=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tcf);
    pos = append_string(pos, " ftf=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_ftf);
    pos = append_string(pos, " tef=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tef);
    pos = append_string(pos, " tof=");
    pos = append_u32_dec(pos, snapshot.qspi_flag_tof);
    pos = append_string(pos, " flvl=");
    pos = append_u32_dec(pos, snapshot.qspi_flevel);
    pos = append_string(pos, " dmaen=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_dmaen);
    pos = append_string(pos, " tcie=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_tcie);
    pos = append_string(pos, " teie=");
    pos = append_u32_dec(pos, snapshot.qspi_cr_teie);
    pos = append_string(pos, " dma_en=");
    pos = append_u32_dec(pos, snapshot.dma_ccr_en);
    pos = append_string(pos, " dma_tc=");
    pos = append_u32_dec(pos, snapshot.dma_isr_tcif);
    pos = append_string(pos, " dma_te=");
    pos = append_u32_dec(pos, snapshot.dma_isr_teif);
    pos = append_string(pos, " ccr=");
    pos = append_u32_hex(pos, snapshot.qspi_ccr);
    pos = append_string(pos, " ar=");
    pos = append_u32_hex(pos, snapshot.qspi_ar);
    pos = append_string(pos, " dlr=");
    pos = append_u32_dec(pos, snapshot.qspi_dlr);
    pos = append_string(pos, " dcndtr=");
    pos = append_u32_dec(pos, snapshot.dma_cndtr);
    pos = append_string(pos, " dstate=");
    pos = append_u32_dec(pos, snapshot.dma_state);
    pos = append_string(pos, " dstatus=");
    pos = append_u32_dec(pos, snapshot.dma_status);
    pos = append_char(pos, '\n');

    (void)board_usb_write(text_buffer, pos, &written);
}

static void send_summary(const char* name,
                         uint32_t packet_count,
                         uint32_t elapsed_ms,
                         uint32_t checksum) {
    size_t pos = 0U;
    uint64_t byte_count;
    uint64_t bytes_per_second;
    size_t written = 0U;

    byte_count = (uint64_t)packet_count * (uint64_t)TEST_PACKET_SIZE;

    if (elapsed_ms == 0U) {
        bytes_per_second = 0ULL;
    } else {
        bytes_per_second = (byte_count * 1000ULL) / (uint64_t)elapsed_ms;
    }

    pos = append_string(pos, name);
    pos = append_string(pos, " packets=");
    pos = append_u32_dec(pos, packet_count);
    pos = append_string(pos, " bytes=");
    pos = append_u64_dec(pos, byte_count);
    pos = append_string(pos, " ms=");
    pos = append_u32_dec(pos, elapsed_ms);
    pos = append_string(pos, " Bps=");
    pos = append_u64_dec(pos, bytes_per_second);
    pos = append_string(pos, " checksum=");
    pos = append_u32_hex(pos, checksum);
    pos = append_char(pos, '\n');

    wait_usb_ready();
    wait_ok(board_usb_write(text_buffer, pos, &written));

    if (written != pos) {
        panic_loop();
    }
}

static void nand_ready(void) {
    wait_ok(board_nand_power_on(TEST_BANK_ID));
    wait_ok(board_nand_connect(TEST_BANK_ID));
}

static void run_usb_only(void) {
    uint32_t packet_index;

    wait_usb_ready();
    timebase_delay_ms_blocking(1000U);

    build_packet_template();

    for (packet_index = 0U; packet_index < TEST_PACKET_COUNT; ++packet_index) {
        stamp_packet(packet_index);
        send_packet();
    }
}

static void run_nand_write_prepare(void) {
    uint32_t packet_index;
    uint32_t start_ms;
    uint32_t elapsed_ms;
    uint32_t checksum = 2166136261UL;
    BoardStatus status;
    uint8_t is_idle;

    nand_ready();

    status = board_nand_erase_start(TEST_BANK_ID);
    if (status != BOARD_OK) {
        debug_send_write_line("ERASE_START_FAIL", 0U, status);
        panic_loop();
    }

    wait_erase_done();

    status = board_nand_open_write(TEST_BANK_ID, 0U);
    if (status != BOARD_OK) {
        debug_send_write_line("OPEN_WRITE_FAIL", 0U, status);
        panic_loop();
    }

    build_packet_template();

    start_ms = timebase_millis();

    for (packet_index = 0U; packet_index < TEST_PACKET_COUNT; ++packet_index) {
        debug_send_write_line("START", packet_index, BOARD_OK);

        stamp_packet(packet_index);
        checksum = checksum_update_packet(checksum);

        status = board_nand_write_packet(TEST_BANK_ID, packet);
        if (status != BOARD_OK) {
            debug_send_write_line("WRITE_FAIL", packet_index, status);
            debug_send_snapshot("WRITE_FAIL", packet_index);
            debug_send_timeout_snapshot("WRITE_FAIL", packet_index);
            panic_loop();
        }

        debug_send_write_line("WRITE_OK", packet_index, BOARD_OK);

        is_idle = 0U;

        while (is_idle == 0U) {
            status = board_nand_write_poll(TEST_BANK_ID, &is_idle);
            if (status != BOARD_OK) {
                debug_send_write_line("POLL_FAIL", packet_index, status);
                debug_send_snapshot("POLL_FAIL", packet_index);
                debug_send_timeout_snapshot("POLL_FAIL", packet_index);
                panic_loop();
            }
        }

        debug_send_write_line("IDLE_OK", packet_index, BOARD_OK);
    }

    status = board_nand_write_flush(TEST_BANK_ID, &is_idle);
    if (status != BOARD_OK) {
        debug_send_write_line("FLUSH_FAIL", TEST_PACKET_COUNT, status);
        debug_send_snapshot("FLUSH_FAIL", TEST_PACKET_COUNT);
        debug_send_timeout_snapshot("FLUSH_FAIL", TEST_PACKET_COUNT);
        panic_loop();
    }

    while (is_idle == 0U) {
        status = board_nand_write_flush(TEST_BANK_ID, &is_idle);
        if (status != BOARD_OK) {
            debug_send_write_line("FLUSH_POLL_FAIL", TEST_PACKET_COUNT, status);
            debug_send_snapshot("FLUSH_POLL_FAIL", TEST_PACKET_COUNT);
            debug_send_timeout_snapshot("FLUSH_POLL_FAIL", TEST_PACKET_COUNT);
            panic_loop();
        }
    }

    elapsed_ms = (uint32_t)(timebase_millis() - start_ms);

    send_summary("NAND_WRITE_PREPARE", TEST_PACKET_COUNT, elapsed_ms, checksum);
}

static void run_nand_read_only(void) {
    uint32_t packet_index;
    uint32_t start_ms;
    uint32_t elapsed_ms;
    uint32_t checksum = 2166136261UL;
    uint8_t has_packet = 0U;

    nand_ready();

    wait_ok(board_nand_open_read(TEST_BANK_ID, TEST_PACKET_COUNT));

    start_ms = timebase_millis();

    for (packet_index = 0U; packet_index < TEST_PACKET_COUNT; ++packet_index) {
        has_packet = 0U;
        wait_ok(board_nand_read_next_packet(TEST_BANK_ID, packet, &has_packet));

        if (has_packet == 0U) {
            panic_loop();
        }

        checksum = checksum_update_packet(checksum);
    }

    elapsed_ms = (uint32_t)(timebase_millis() - start_ms);

    send_summary("NAND_READ_ONLY", TEST_PACKET_COUNT, elapsed_ms, checksum);
}

static void run_nand_usb_dump(void) {
    uint32_t packet_index;
    uint8_t has_packet = 0U;

    static uint8_t buffers[2][TEST_PACKET_SIZE];
    uint8_t curr = 0;
    uint8_t next = 1;

    nand_ready();
    wait_usb_ready();
    timebase_delay_ms_blocking(5000U);

    wait_ok(board_nand_open_read(TEST_BANK_ID, TEST_PACKET_COUNT));

    wait_ok(board_nand_read_next_packet(TEST_BANK_ID, buffers[curr], &has_packet));
    if (has_packet == 0U) panic_loop();

    for (packet_index = 0U; packet_index < TEST_PACKET_COUNT; ++packet_index) {

        if (packet_index < (TEST_PACKET_COUNT - 1)) {
            wait_ok(board_nand_read_next_packet(TEST_BANK_ID, buffers[next], &has_packet));
            if (has_packet == 0U) panic_loop();
        }

        size_t written = 0U;
        wait_ok(board_usb_write(buffers[curr], TEST_PACKET_SIZE, &written));
        if (written != TEST_PACKET_SIZE) panic_loop();

        curr = next;
        next = 1 - curr;
    }
}

int main(void) {
    wait_ok(clock_init());
    wait_ok(timebase_init());
    wait_ok(board_init_hardware());

#if (TEST_MODE == TEST_MODE_USB_ONLY)
    run_usb_only();
#elif (TEST_MODE == TEST_MODE_NAND_READ_ONLY)
    run_nand_read_only();
#elif (TEST_MODE == TEST_MODE_NAND_USB_DUMP)
    run_nand_usb_dump();
#elif (TEST_MODE == TEST_MODE_NAND_WRITE_PREPARE)
    run_nand_write_prepare();
#else
    panic_loop();
#endif

    while (1) {
        __asm volatile ("nop");
    }
}
