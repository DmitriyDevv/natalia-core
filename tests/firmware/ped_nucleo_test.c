/*
 * Стенд ПЭД на NUCLEO-L496ZG: только PED_REG, журнал через LPUART1
 * (виртуальный COM-порт ST-LINK, PG7/PG8).
 *
 * Сборка: пресет `ped-nucleo` (CMakePresets.json) или вручную:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ped_nucleo_test.c
 *   -DNATALIA_ENABLE_PED_REG_DRIVER=ON -DNATALIA_LOG_BACKEND=LPUART1
 *   -DNATALIA_LPUART1_BAUDRATE=921600 -DNATALIA_USE_BOARD_STUBS=OFF
 *   все остальные драйверы OFF, NATALIA_NAND_POWER_SWITCH=NONE,
 *   NATALIA_ENABLE_WATCHDOG=OFF.
 *
 * Порядок работы:
 *   1. 5 с ожидания (чтобы успеть запустить приёмник на ПК);
 *   2. board_init_hardware(): выводы в безопасное состояние, RTC (LSE, 1 Гц на
 *      PB2 = RTC_OUT), PED_REG;
 *   3. board_ped_power_on(): PS (PE6) -> 200 мс -> PSON (PG14) -> READY (PG0)
 *      -> Status_L/H (00h/01h, ожидается 455Ah/5941h); пауза 2 с после
 *      выключения выдерживает Board_API;
 *   4. запись Conf_Trig (80h) и Threshold (81h), SLEEP = 0, INHIBIT = 0;
 *   5. сбор: события по фронту PED_TG (PG13), счётчики по фронту 1 Гц (PB2).
 *
 * Без ключа питания ПЭД: питать ПЭД от лабораторного источника и поставить
 * перемычку PE6 -> PG14, тогда PSON повторяет команду PS.
 *
 * Строки журнала (одна запись на строку, поля через запятую):
 *   # текст                       сообщение для человека
 *   I,<ключ>,<значение>           сведения о сборке и настройке
 *   S,<с>,<флаги>,<N_D>,<N_AC1>,<N_AC2>,<N_Trig>,<T_S_Dead>,<соб>,<сыр>,<удерж>,<пик>,<потер>
 *                                 секунда: время RTC, флаги записи (hex),
 *                                 счётчики ПЭД, событий МК за секунду, из них
 *                                 выведено строками E, удержано всего, пик
 *                                 кольца, потерянных секундных отметок
 *   E,<T_Trig>,<T_PE_Dead>,<Amp_D>,<Trig_Stat>
 *                                 событие (десятичные, Trig_Stat — hex)
 *   H,<сдвиг>,<всего>,<непустых>  начало гистограммы Amp_D (накопленной)
 *   h,<корзина>:<число>,...       корзины гистограммы, корзина = Amp_D >> сдвиг
 *   B,<b0>,...,<b15>              сколько раз взведён каждый бит Trig_Stat
 *   F,<маска>                     неисправность ПЭД (1 питание, 2 READY, 4 статус)
 *   R,<команда>,<код>             ответ на команду (0 — успешно)
 *
 * Команды с ПК (строка, конец — \r или \n):
 *   help | stat | time | thr <0..15> | trig <hex> | wr <адрес hex> <значение hex>
 *   inh <0|1> | sleep <0|1> | raw <число событий E в секунду> | hist | clr
 *   on | off | restart
 *   диагностика: pins (уровни PE6/PG14/PG0/PG13/PB2, VDDIO2) | ps <0|1> (PE6
 *   напрямую, мимо драйвера) | pull <n|u|d> (подтяжка PG14)
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_api.h"
#include "clock.h"
#include "debug_log.h"
#include "natalia_build_info.h"
#include "ped_reg.h"
#include "stm32l4xx.h"
#include "timebase.h"

#ifndef PED_NT_CONF_TRIG
#define PED_NT_CONF_TRIG 0x0000U
#endif

#ifndef PED_NT_THRESHOLD
#define PED_NT_THRESHOLD 0x0000U
#endif

#ifndef PED_NT_RAW_PER_SECOND
#define PED_NT_RAW_PER_SECOND 200U
#endif

#ifndef PED_NT_HIST_SHIFT
#define PED_NT_HIST_SHIFT 6U
#endif

#ifndef PED_NT_HIST_PERIOD_S
#define PED_NT_HIST_PERIOD_S 10U
#endif

#ifndef PED_NT_NUCLEO_LEDS
#define PED_NT_NUCLEO_LEDS 1
#endif

#define PED_NT_START_DELAY_MS   5000UL
#define PED_NT_TAKE_CAPACITY    64U
#define PED_NT_HIST_BINS        (65536UL >> PED_NT_HIST_SHIFT)
#define PED_NT_HIST_PER_LINE    16U
#define PED_NT_NO_SECOND_MS     2500UL
#define PED_NT_CMD_MAX          48U
#define PED_NT_RX_SIZE          128U
#define PED_NT_RX_IRQ_PRIORITY  4U
#define PED_NT_RAW_MAX          20000U
#define PED_NT_ADDR_CONF_TRIG   0x80U
#define PED_NT_ADDR_THRESHOLD   0x81U
#define PED_NT_STATUS_EXPECTED  0x5941455AUL

#define PED_NT_LED_GREEN_PORT   GPIOC
#define PED_NT_LED_GREEN_PIN    7U
#define PED_NT_LED_BLUE_PORT    GPIOB
#define PED_NT_LED_BLUE_PIN     7U
#define PED_NT_LED_RED_PORT     GPIOB
#define PED_NT_LED_RED_PIN      14U

static BoardPedRecord nt_records[PED_NT_TAKE_CAPACITY];
static uint32_t nt_hist[PED_NT_HIST_BINS];
static uint32_t nt_bits[16];
static uint32_t nt_hist_total;

static uint32_t nt_raw_limit = PED_NT_RAW_PER_SECOND;
static uint32_t nt_second_events;
static uint32_t nt_second_raw;
static uint32_t nt_seconds_since_dump;
static uint32_t nt_last_second_ms;
static uint32_t nt_last_warn_ms;
static uint8_t nt_powered;

static volatile uint8_t nt_rx[PED_NT_RX_SIZE];
static volatile uint32_t nt_rx_head;
static volatile uint32_t nt_rx_tail;
static char nt_cmd[PED_NT_CMD_MAX];
static uint32_t nt_cmd_length;

static void nt_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

static void nt_line(const char* format, ...) {
    char text[200];
    va_list args;

    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    debug_log_write(text);
}

static void nt_led_init(void) {
#if PED_NT_NUCLEO_LEDS
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;

    PED_NT_LED_GREEN_PORT->MODER &= ~(3UL << (PED_NT_LED_GREEN_PIN * 2U));
    PED_NT_LED_GREEN_PORT->MODER |= 1UL << (PED_NT_LED_GREEN_PIN * 2U);
    PED_NT_LED_BLUE_PORT->MODER &= ~(3UL << (PED_NT_LED_BLUE_PIN * 2U));
    PED_NT_LED_BLUE_PORT->MODER |= 1UL << (PED_NT_LED_BLUE_PIN * 2U);
    PED_NT_LED_RED_PORT->MODER &= ~(3UL << (PED_NT_LED_RED_PIN * 2U));
    PED_NT_LED_RED_PORT->MODER |= 1UL << (PED_NT_LED_RED_PIN * 2U);

    PED_NT_LED_GREEN_PORT->BSRR = 1UL << (PED_NT_LED_GREEN_PIN + 16U);
    PED_NT_LED_BLUE_PORT->BSRR = 1UL << (PED_NT_LED_BLUE_PIN + 16U);
    PED_NT_LED_RED_PORT->BSRR = 1UL << (PED_NT_LED_RED_PIN + 16U);
#endif
}

static void nt_led_set(GPIO_TypeDef* port, uint32_t pin, uint8_t on) {
#if PED_NT_NUCLEO_LEDS
    port->BSRR = (on != 0U) ? (1UL << pin) : (1UL << (pin + 16U));
#else
    (void)port;
    (void)pin;
    (void)on;
#endif
}

static void nt_led_toggle(GPIO_TypeDef* port, uint32_t pin) {
#if PED_NT_NUCLEO_LEDS
    port->ODR ^= 1UL << pin;
#else
    (void)port;
    (void)pin;
#endif
}

static void nt_clear_statistics(void) {
    (void)memset(nt_hist, 0, sizeof(nt_hist));
    (void)memset(nt_bits, 0, sizeof(nt_bits));
    nt_hist_total = 0U;
}

static void nt_dump_histogram(void) {
    char text[200];
    uint32_t bin;
    uint32_t nonzero = 0U;
    uint32_t in_line = 0U;
    size_t length = 0U;

    for (bin = 0U; bin < PED_NT_HIST_BINS; ++bin) {
        if (nt_hist[bin] != 0U) {
            ++nonzero;
        }
    }

    nt_line("H,%u,%lu,%lu\n", (unsigned)PED_NT_HIST_SHIFT,
            (unsigned long)nt_hist_total, (unsigned long)nonzero);

    for (bin = 0U; bin < PED_NT_HIST_BINS; ++bin) {
        if (nt_hist[bin] == 0U) {
            continue;
        }
        if (in_line == 0U) {
            length = (size_t)snprintf(text, sizeof(text), "h");
        }
        length += (size_t)snprintf(&text[length], sizeof(text) - length, ",%lu:%lu",
                                   (unsigned long)bin, (unsigned long)nt_hist[bin]);
        ++in_line;
        if (in_line == PED_NT_HIST_PER_LINE) {
            debug_log_write(text);
            debug_log_write("\n");
            in_line = 0U;
        }
    }
    if (in_line != 0U) {
        debug_log_write(text);
        debug_log_write("\n");
    }

    nt_line("B,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long)nt_bits[0], (unsigned long)nt_bits[1], (unsigned long)nt_bits[2],
            (unsigned long)nt_bits[3], (unsigned long)nt_bits[4], (unsigned long)nt_bits[5],
            (unsigned long)nt_bits[6], (unsigned long)nt_bits[7], (unsigned long)nt_bits[8],
            (unsigned long)nt_bits[9], (unsigned long)nt_bits[10], (unsigned long)nt_bits[11],
            (unsigned long)nt_bits[12], (unsigned long)nt_bits[13], (unsigned long)nt_bits[14],
            (unsigned long)nt_bits[15]);
}

static void nt_print_status(void) {
    uint32_t status = 0U;
    uint8_t powered = 0U;
    BoardPedStats stats;
    BoardStatus result;

    (void)memset(&stats, 0, sizeof(stats));

    (void)board_ped_is_powered(&powered);
    result = board_ped_read_status(&status);
    (void)board_ped_get_stats(&stats);

    nt_line("I,pson,%u\n", (unsigned)powered);
    nt_line("I,status_read,%d\n", (int)result);
    nt_line("I,status,%08lX,%s\n", (unsigned long)status,
            (status == PED_NT_STATUS_EXPECTED) ? "ok" : "MISMATCH");
    nt_line("I,stats,events_read=%lu,events_held=%lu,seconds=%lu,seconds_lost=%lu,ring=%lu,ring_peak=%lu\n",
            (unsigned long)stats.events_read, (unsigned long)stats.events_held,
            (unsigned long)stats.seconds_marked, (unsigned long)stats.seconds_lost,
            (unsigned long)stats.ring_count, (unsigned long)stats.ring_high_water);
}

static BoardStatus nt_write_register(uint8_t address, uint16_t value) {
    BoardStatus status = board_ped_write_register(address, value);

    nt_line("I,write,%02X,%04X,%d\n", (unsigned)address, (unsigned)value, (int)status);
    return status;
}

static uint32_t nt_pin(GPIO_TypeDef* port, uint32_t pin) {
    return (port->IDR >> pin) & 1UL;
}

static void nt_print_pins(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;

    nt_line("I,pins,PE6_out=%lu,PE6_in=%lu,PE6_mode=%lu,PG14=%lu,PG14_mode=%lu,PG14_pull=%lu,"
            "PG0=%lu,PG13=%lu,PB2=%lu,VDDIO2=%lu\n",
            (unsigned long)((GPIOE->ODR >> 6U) & 1UL), (unsigned long)nt_pin(GPIOE, 6U),
            (unsigned long)((GPIOE->MODER >> 12U) & 3UL), (unsigned long)nt_pin(GPIOG, 14U),
            (unsigned long)((GPIOG->MODER >> 28U) & 3UL), (unsigned long)((GPIOG->PUPDR >> 28U) & 3UL),
            (unsigned long)nt_pin(GPIOG, 0U), (unsigned long)nt_pin(GPIOG, 13U),
            (unsigned long)nt_pin(GPIOB, 2U), (unsigned long)((PWR->CR2 & PWR_CR2_IOSV) != 0U));
}

static void nt_drive_ps(uint32_t on) {
    GPIOE->MODER &= ~(3UL << 12U);
    GPIOE->MODER |= 1UL << 12U;
    GPIOE->BSRR = (on != 0U) ? (1UL << 6U) : (1UL << 22U);
    timebase_delay_ms_blocking(10U);
}

static void nt_set_pson_pull(char mode) {
    uint32_t pull = (mode == 'u') ? 1UL : ((mode == 'd') ? 2UL : 0UL);

    GPIOG->PUPDR = (GPIOG->PUPDR & ~(3UL << 28U)) | (pull << 28U);
    timebase_delay_ms_blocking(2U);
}

static const char* nt_stage_name(uint8_t stage) {
    switch (stage) {
    case PED_REG_POWER_ON_PSON_HIGH_BEFORE:
        return "PSON_HIGH_BEFORE_PS";
    case PED_REG_POWER_ON_PSON_LOW_AFTER:
        return "PSON_LOW_200MS_AFTER_PS";
    case PED_REG_POWER_ON_READY_HIGH:
        return "READY_HIGH";
    case PED_REG_POWER_ON_STATUS_MISMATCH:
        return "STATUS_MISMATCH";
    case PED_REG_POWER_ON_OK:
        return "OK";
    default:
        return "NOT_RUN";
    }
}

static void nt_print_power_on_report(void) {
    PedRegPowerOnReport report = {0U, 0U, 0U, 0U, 0U};
    uint32_t faults = 0U;

    (void)ped_reg_get_power_on_report(&report);
    nt_line("I,power_on_stage,%u,%s\n", (unsigned)report.stage, nt_stage_name(report.stage));
    nt_line("I,power_on_levels,pson_before=%u,pson_after=%u,ready_low=%u\n",
            (unsigned)report.pson_before, (unsigned)report.pson_after, (unsigned)report.ready_low);
    nt_line("I,status_at_power_on,%08lX,%s\n", (unsigned long)report.status,
            (report.status == PED_NT_STATUS_EXPECTED) ? "ok" : "MISMATCH");

    if (report.stage == PED_REG_POWER_ON_OK) {
        return;
    }

    (void)board_ped_take_faults(&faults);
    nt_line("F,%02lX\n", (unsigned long)faults);
    nt_print_pins();

    switch (report.stage) {
    case PED_REG_POWER_ON_PSON_HIGH_BEFORE:
        nt_line("# PSON (PG14, CN12-61) was HIGH before PS: it is tied to 3V3 or to a signal that\n");
        nt_line("#   is always on. Try 'pull d' + 'pins': still 1 -> driven from outside.\n");
        break;
    case PED_REG_POWER_ON_PSON_LOW_AFTER:
        nt_line("# PSON (PG14, CN12-61) stayed LOW 200 ms after PS (PE6, CN11-62); PS switched off.\n");
        nt_line("#   Check: 'ps 1' then 'pins' -> PE6_in must be 1 and PG14 must be 1 (jumper);\n");
        nt_line("#   'pull u'/'pull d' changes PG14 -> the line is not connected. Then 'ps 0'.\n");
        break;
    case PED_REG_POWER_ON_READY_HIGH:
        nt_line("# READY (PG0, CN11-59) is HIGH after power-on: PED not ready or not connected\n");
        break;
    case PED_REG_POWER_ON_STATUS_MISMATCH:
        nt_line("# status: expected 5941455A; check data PD0..15, address PF0..7, DIR PF8\n");
        break;
    default:
        break;
    }
}

static BoardStatus nt_power_up(void) {
    BoardStatus result;

    nt_line("# PED power on (PS PE6 -> 200 ms -> PSON PG14 -> READY PG0 -> Status)\n");
    nt_print_pins();
    result = board_ped_power_on();
    nt_line("I,power_on,%d\n", (int)result);
    nt_print_power_on_report();
    if (result != BOARD_OK) {
        nt_led_set(PED_NT_LED_RED_PORT, PED_NT_LED_RED_PIN, 1U);
        return result;
    }

    (void)nt_write_register(PED_NT_ADDR_CONF_TRIG, (uint16_t)PED_NT_CONF_TRIG);
    (void)nt_write_register(PED_NT_ADDR_THRESHOLD, (uint16_t)PED_NT_THRESHOLD);

    (void)board_ped_set_sleep(0U);
    (void)board_ped_set_inhibit(0U);
    nt_line("I,sleep,0\nI,inhibit,0\n");

    result = board_ped_acquisition_start();
    nt_line("I,acquisition_start,%d\n", (int)result);

    nt_powered = 1U;
    nt_second_events = 0U;
    nt_second_raw = 0U;
    nt_led_set(PED_NT_LED_GREEN_PORT, PED_NT_LED_GREEN_PIN, 1U);
    nt_led_set(PED_NT_LED_RED_PORT, PED_NT_LED_RED_PIN, 0U);

    return result;
}

static void nt_power_down(void) {
    (void)board_ped_set_inhibit(1U);
    (void)board_ped_power_off();
    nt_powered = 0U;
    nt_line("I,power_off,0\n");
    nt_led_set(PED_NT_LED_GREEN_PORT, PED_NT_LED_GREEN_PIN, 0U);
}

static void nt_on_event(const BoardPedRecord* record) {
    uint16_t amp = record->data[2];
    uint16_t stat = record->data[3];
    uint32_t bit;

    ++nt_second_events;
    ++nt_hist[amp >> PED_NT_HIST_SHIFT];
    ++nt_hist_total;

    for (bit = 0U; bit < 16U; ++bit) {
        if ((stat & (1U << bit)) != 0U) {
            ++nt_bits[bit];
        }
    }

    if (nt_second_raw < nt_raw_limit) {
        ++nt_second_raw;
        nt_line("E,%u,%u,%u,%04X\n", (unsigned)record->data[0], (unsigned)record->data[1],
                (unsigned)amp, (unsigned)stat);
    }
}

static void nt_on_second(const BoardPedRecord* record) {
    BoardPedStats stats;

    (void)memset(&stats, 0, sizeof(stats));
    (void)board_ped_get_stats(&stats);

    nt_line("S,%lu,%02X,%u,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu\n",
            (unsigned long)record->rtc_seconds, (unsigned)record->flags,
            (unsigned)record->data[0], (unsigned)record->data[1], (unsigned)record->data[2],
            (unsigned)record->data[3], (unsigned)record->data[4],
            (unsigned long)nt_second_events, (unsigned long)nt_second_raw,
            (unsigned long)stats.events_held, (unsigned long)stats.ring_high_water,
            (unsigned long)stats.seconds_lost);

    nt_second_events = 0U;
    nt_second_raw = 0U;
    nt_last_second_ms = timebase_millis();
    nt_led_toggle(PED_NT_LED_BLUE_PORT, PED_NT_LED_BLUE_PIN);

    ++nt_seconds_since_dump;
    if (nt_seconds_since_dump >= PED_NT_HIST_PERIOD_S) {
        nt_seconds_since_dump = 0U;
        nt_dump_histogram();
    }
}

static void nt_pump_records(void) {
    size_t count = 0U;
    size_t i;

    if (board_ped_take_records(nt_records, PED_NT_TAKE_CAPACITY, &count) != BOARD_OK) {
        return;
    }

    for (i = 0U; i < count; ++i) {
        if (nt_records[i].kind == BOARD_PED_RECORD_EVENT) {
            nt_on_event(&nt_records[i]);
        } else if (nt_records[i].kind == BOARD_PED_RECORD_SECOND) {
            nt_on_second(&nt_records[i]);
        }
    }
}

static void nt_check_faults(void) {
    uint32_t faults = 0U;

    if ((board_ped_take_faults(&faults) == BOARD_OK) && (faults != 0U)) {
        nt_line("F,%02lX\n", (unsigned long)faults);
        nt_line("# PED fault: bus stopped; send 'restart' after fixing\n");
        nt_led_set(PED_NT_LED_RED_PORT, PED_NT_LED_RED_PIN, 1U);
    }
}

static void nt_check_seconds(void) {
    uint32_t now = timebase_millis();

    if (nt_powered == 0U) {
        nt_last_second_ms = now;
        return;
    }

    if (((now - nt_last_second_ms) >= PED_NT_NO_SECOND_MS) &&
        ((now - nt_last_warn_ms) >= PED_NT_NO_SECOND_MS)) {
        nt_last_warn_ms = now;
        nt_line("# no 1 Hz second mark for %lu ms (RTC_OUT PB2, LSE)\n",
                (unsigned long)(now - nt_last_second_ms));
    }
}

static int nt_parse_hex(const char* text, uint32_t* value) {
    char* end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (*text == '\0')) {
        return 0;
    }
    parsed = strtoul(text, &end, 16);
    if ((end == text) || ((*end != '\0') && (*end != ' '))) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int nt_parse_dec(const char* text, uint32_t* value) {
    char* end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (*text == '\0')) {
        return 0;
    }
    parsed = strtoul(text, &end, 10);
    if ((end == text) || ((*end != '\0') && (*end != ' '))) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static const char* nt_argument(const char* line) {
    const char* space = strchr(line, ' ');

    if (space == NULL) {
        return NULL;
    }
    while (*space == ' ') {
        ++space;
    }
    return space;
}

static void nt_print_help(void) {
    nt_line("# commands: help stat time thr <0..15> trig <hex> wr <addr hex> <value hex>\n");
    nt_line("#           inh <0|1> sleep <0|1> raw <n> hist clr on off restart\n");
    nt_line("#   diagnostics: pins | ps <0|1> (drive PE6 directly) | pull <n|u|d> (PG14 pull)\n");
}

static void nt_execute(const char* line) {
    const char* arg = nt_argument(line);
    uint32_t value = 0U;
    uint32_t address = 0U;
    int result = 0;
    char name[8];
    size_t i;

    for (i = 0U; (i < (sizeof(name) - 1U)) && (line[i] != '\0') && (line[i] != ' '); ++i) {
        name[i] = line[i];
    }
    name[i] = '\0';

    if (strcmp(name, "help") == 0) {
        nt_print_help();
    } else if (strcmp(name, "stat") == 0) {
        nt_print_status();
    } else if (strcmp(name, "time") == 0) {
        InstrumentTime now = {0U, 0U};
        (void)board_rtc_get_time(&now);
        nt_line("I,rtc,%lu.%03u,uptime_ms=%lu\n", (unsigned long)now.seconds,
                (unsigned)now.milliseconds, (unsigned long)timebase_millis());
    } else if ((strcmp(name, "thr") == 0) && nt_parse_dec(arg, &value) && (value <= 15U)) {
        result = (int)nt_write_register(PED_NT_ADDR_THRESHOLD, (uint16_t)value);
    } else if ((strcmp(name, "trig") == 0) && nt_parse_hex(arg, &value) && (value <= 0xFFFFU)) {
        result = (int)nt_write_register(PED_NT_ADDR_CONF_TRIG, (uint16_t)value);
    } else if ((strcmp(name, "wr") == 0) && nt_parse_hex(arg, &address) && (address <= 0xFFU) &&
               nt_parse_hex(nt_argument(arg), &value) && (value <= 0xFFFFU)) {
        result = (int)nt_write_register((uint8_t)address, (uint16_t)value);
    } else if ((strcmp(name, "inh") == 0) && nt_parse_dec(arg, &value) && (value <= 1U)) {
        result = (int)board_ped_set_inhibit((uint8_t)value);
    } else if ((strcmp(name, "sleep") == 0) && nt_parse_dec(arg, &value) && (value <= 1U)) {
        result = (int)board_ped_set_sleep((uint8_t)value);
    } else if ((strcmp(name, "raw") == 0) && nt_parse_dec(arg, &value) && (value <= PED_NT_RAW_MAX)) {
        nt_raw_limit = value;
    } else if (strcmp(name, "pins") == 0) {
        nt_print_pins();
    } else if ((strcmp(name, "ps") == 0) && nt_parse_dec(arg, &value) && (value <= 1U)) {
        nt_drive_ps(value);
        nt_print_pins();
    } else if ((strcmp(name, "pull") == 0) && (arg != NULL) &&
               ((arg[0] == 'n') || (arg[0] == 'u') || (arg[0] == 'd'))) {
        nt_set_pson_pull(arg[0]);
        nt_print_pins();
    } else if (strcmp(name, "hist") == 0) {
        nt_dump_histogram();
    } else if (strcmp(name, "clr") == 0) {
        nt_clear_statistics();
    } else if (strcmp(name, "on") == 0) {
        result = nt_powered ? 0 : (int)nt_power_up();
    } else if (strcmp(name, "off") == 0) {
        nt_power_down();
    } else if (strcmp(name, "restart") == 0) {
        nt_power_down();
        result = (int)nt_power_up();
    } else {
        nt_line("R,%s,unknown\n", line);
        nt_print_help();
        return;
    }

    nt_line("R,%s,%d\n", line, result);
}

void LPUART1_IRQHandler(void);

void LPUART1_IRQHandler(void) {
    uint32_t isr = LPUART1->ISR;
    uint32_t next;
    uint8_t byte;

    if ((isr & USART_ISR_ORE) != 0U) {
        LPUART1->ICR = USART_ICR_ORECF;
    }

    if ((isr & USART_ISR_RXNE) != 0U) {
        byte = (uint8_t)(LPUART1->RDR & 0xFFU);
        next = (nt_rx_head + 1U) % PED_NT_RX_SIZE;
        if (next != nt_rx_tail) {
            nt_rx[nt_rx_head] = byte;
            nt_rx_head = next;
        }
    }
}

static void nt_rx_start(void) {
    nt_rx_head = 0U;
    nt_rx_tail = 0U;
    LPUART1->ICR = USART_ICR_ORECF;
    LPUART1->CR1 |= USART_CR1_RXNEIE;
    NVIC_SetPriority(LPUART1_IRQn, PED_NT_RX_IRQ_PRIORITY);
    NVIC_EnableIRQ(LPUART1_IRQn);
}

static void nt_poll_commands(void) {
    char c;

    while (nt_rx_tail != nt_rx_head) {
        c = (char)nt_rx[nt_rx_tail];
        nt_rx_tail = (nt_rx_tail + 1U) % PED_NT_RX_SIZE;

        if ((c == '\r') || (c == '\n')) {
            if (nt_cmd_length > 0U) {
                nt_cmd[nt_cmd_length] = '\0';
                nt_execute(nt_cmd);
                nt_cmd_length = 0U;
            }
        } else if (nt_cmd_length < (PED_NT_CMD_MAX - 1U)) {
            nt_cmd[nt_cmd_length] = c;
            ++nt_cmd_length;
        }
    }
}

int main(void) {
    BoardStatus status;

    (void)clock_init();
    (void)timebase_init();
    (void)debug_log_init();
    nt_rx_start();

    timebase_delay_ms_blocking(PED_NT_START_DELAY_MS);

    nt_line("\n# ===== PED NUCLEO TEST =====\n");
    nt_line("I,build,%s\n", NATALIA_BUILD_TIME);
    nt_line("I,sysclk,%lu\n", (unsigned long)clock_get_sysclk_hz());
    nt_line("I,conf_trig,%04X\nI,threshold,%u\n", (unsigned)PED_NT_CONF_TRIG,
            (unsigned)PED_NT_THRESHOLD);
    nt_line("I,raw_per_second,%lu\nI,hist_shift,%u\nI,hist_period_s,%u\n",
            (unsigned long)nt_raw_limit, (unsigned)PED_NT_HIST_SHIFT,
            (unsigned)PED_NT_HIST_PERIOD_S);

    status = board_init_hardware();
    nt_line("I,board_init_hardware,%d\n", (int)status);
    if (status != BOARD_OK) {
        nt_line("# board_init_hardware failed (RTC needs the LSE crystal X2)\n");
    }

    nt_led_init();
    nt_clear_statistics();
    nt_print_help();

    (void)nt_power_up();
    nt_last_second_ms = timebase_millis();
    nt_last_warn_ms = nt_last_second_ms;

    for (;;) {
        nt_pump_records();
        nt_check_faults();
        nt_check_seconds();
        nt_poll_commands();
    }
}
