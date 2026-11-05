/*
 * Independent watchdog check: the MCU must reset when the main loop stops kicking.
 *
 * After the usual 5 s start delay the program prints the reset cause flags from
 * RCC_CSR (then clears them), starts the IWDG through watchdog_init() exactly as
 * main.c does, kicks it once per second for WT_KICK_S seconds and then stops
 * kicking. With the watchdog working the MCU resets about 4 s later and the next
 * start prints iwdg=1.
 *
 * Log lines: "watchdog test start", reset flags "rst iwdg=<0|1> wwdg= sft= pin= bor=
 * lpwr=", watchdog_init=<status>, "kick t=<s>" every second, "hang: kicking stopped",
 * then, if no reset happens, "NO RESET t=<s>" every second.
 *
 * Build: -DNATALIA_FIRMWARE_MAIN=tests/firmware/watchdog_test.c
 *        -DNATALIA_ENABLE_WATCHDOG=ON -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdint.h>

#include "clock.h"
#include "debug_log.h"
#include "stm32l496xx.h"
#include "timebase.h"
#include "watchdog.h"

#define WT_START_DELAY_MS 5000UL
#define WT_KICK_S         10UL

static void log_kv(const char *key, uint32_t value) {
    debug_log_write(key);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
}

static void log_reset_flags(void) {
    uint32_t csr = RCC->CSR;

    debug_log_write("rst ");
    log_kv("iwdg", ((csr & RCC_CSR_IWDGRSTF) != 0U) ? 1U : 0U);
    debug_log_write(" ");
    log_kv("wwdg", ((csr & RCC_CSR_WWDGRSTF) != 0U) ? 1U : 0U);
    debug_log_write(" ");
    log_kv("sft", ((csr & RCC_CSR_SFTRSTF) != 0U) ? 1U : 0U);
    debug_log_write(" ");
    log_kv("pin", ((csr & RCC_CSR_PINRSTF) != 0U) ? 1U : 0U);
    debug_log_write(" ");
    log_kv("bor", ((csr & RCC_CSR_BORRSTF) != 0U) ? 1U : 0U);
    debug_log_write(" ");
    log_kv("lpwr", ((csr & RCC_CSR_LPWRRSTF) != 0U) ? 1U : 0U);
    debug_log_write("\r\n");

    RCC->CSR |= RCC_CSR_RMVF;
}

int main(void) {
    uint32_t second;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();

    timebase_delay_ms_blocking(WT_START_DELAY_MS);

    debug_log_write("\r\nwatchdog test start\r\n");
    log_reset_flags();

    log_kv("watchdog_init", (uint32_t)watchdog_init());
    debug_log_write("\r\n");

    for (second = 1U; second <= WT_KICK_S; ++second) {
        timebase_delay_ms_blocking(1000UL);
        watchdog_kick();
        log_kv("kick t", second);
        debug_log_write("\r\n");
    }

    debug_log_write("hang: kicking stopped\r\n");

    for (second = 1U;; ++second) {
        timebase_delay_ms_blocking(1000UL);
        log_kv("NO RESET t", second);
        debug_log_write("\r\n");
    }
}
