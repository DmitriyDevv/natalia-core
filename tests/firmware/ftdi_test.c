#include <stddef.h>
#include <stdint.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "ftdi.h"
#include "status.h"
#include "stm32l496xx.h"
#include "timebase.h"

static void log_status(const char *label, BoardStatus status) {
    debug_log_write(label);
    debug_log_write("=");
    debug_log_write_u32_inline((uint32_t)status);
    debug_log_write("\r\n");
}

static void log_hex32(const char *label, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char text[9];
    int i;

    for (i = 7; i >= 0; --i) {
        text[7 - i] = hex[(value >> (i * 4)) & 0xFU];
    }
    text[8] = '\0';

    debug_log_write(label);
    debug_log_write("=0x");
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void log_ftdi_registers(void) {
    log_hex32("GPIOA_MODER", GPIOA->MODER);
    log_hex32("GPIOA_AFRH", GPIOA->AFR[1]);
    log_hex32("USART1_CR1", USART1->CR1);
    log_hex32("USART1_CR3", USART1->CR3);
    log_hex32("USART1_BRR", USART1->BRR);
    log_hex32("USART1_ISR", USART1->ISR);
}

int main(void) {
    static const char data_line[] = "NATALIA-FTDI-DATA-0123456789\r\n";
    BoardStatus status;
    uint32_t iteration = 0U;

    clock_init();
    timebase_init();

    (void)debug_log_init();
    debug_log_write("\r\nftdi test start\r\n");

    status = board_init_hardware();
    log_status("board_init_hardware", status);

    debug_log_write("ftdi_power_present=");
    debug_log_write_u32_inline((uint32_t)ftdi_power_present());
    debug_log_write("\r\n");

    log_ftdi_registers();

    while (1) {
        size_t written = 0U;
        uint8_t idle = 0U;

        status = board_data_write(data_line, sizeof(data_line) - 1U, &written);

        (void)ftdi_flush(1000U);
        (void)ftdi_is_tx_idle(&idle);

        debug_log_write("iter=");
        debug_log_write_u32_inline(iteration);
        debug_log_write(" data_write=");
        debug_log_write_u32_inline((uint32_t)status);
        debug_log_write(" written=");
        debug_log_write_u32_inline((uint32_t)written);
        debug_log_write(" idle=");
        debug_log_write_u32_inline((uint32_t)idle);
        debug_log_write(" dropped=");
        debug_log_write_u32_inline(ftdi_log_dropped_count());
        debug_log_write("\r\n");

        ++iteration;
        timebase_delay_ms_blocking(1000U);
    }
}
