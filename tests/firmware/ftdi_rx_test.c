/*
 * FTDI receive test: PC -> FT232RL -> PA10 (USART1_RX) -> MCU.
 *
 * Same power-up sequence and same USART1 configuration as ftdi_test.c,
 * but nothing is transmitted: everything received is printed to the CAN
 * debug log. Purpose: check whether the FTDI / cable / host path works
 * while PA9 (TX) is suspect.
 *
 * Build:
 *   -DNATALIA_FIRMWARE_MAIN=tests/firmware/ftdi_rx_test.c
 *   -DNATALIA_ENABLE_FTDI_DRIVER=ON
 *   -DNATALIA_LOG_BACKEND=CAN
 */

#include <stdint.h>

#include "clock.h"
#include "debug_log.h"
#include "stm32l496xx.h"
#include "timebase.h"

/* ------------------------------------------------------------------ */
/*  Конфигурация                                                       */
/* ------------------------------------------------------------------ */

#define USE_HW_FLOW_CONTROL   0

#define FTDI_PS_PORT     GPIOC      /* PU_FTDI_PS   (OUT) = PC6 */
#define FTDI_PS_PIN      6u
#define FTDI_RES_PORT    GPIOC      /* PU_FTDI_RES  (OUT) = PC7 */
#define FTDI_RES_PIN     7u
#define FTDI_PSON_PORT   GPIOG      /* PU_FTDI_PSON (IN)  = PG8 */
#define FTDI_PSON_PIN    8u

#define USE_VBUS_CHECK   1
#define USB_VBUS_PORT    GPIOG      /* PU_USB_VBUS  (IN)  = PG7 */
#define USB_VBUS_PIN     7u

#define FTDI_BAUD_BRR    80u        /* PCLK2 80 МГц / 1 000 000 = 80 */

#define RX_BUF_SIZE      32u
#define RX_FLUSH_IDLE_MS 100u
#define HEARTBEAT_MS     2000u

/* ------------------------------------------------------------------ */
/*  Состояние приёма                                                   */
/* ------------------------------------------------------------------ */

static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_len;
static uint32_t rx_total;
static uint32_t rx_error_count;
static uint32_t rx_last_err_isr;

void FTDI_InitPins(void);
int  FTDI_PowerOn(void);
void UART1_Init(void);

/* ------------------------------------------------------------------ */
/*  Логи                                                               */
/* ------------------------------------------------------------------ */

static void log_step(const char *tag, const char *text) {
    debug_log_write(tag);
    debug_log_write(" ");
    debug_log_write(text);
    debug_log_write("\r\n");
}

static void log_step_u32(const char *tag, const char *text, uint32_t value) {
    debug_log_write(tag);
    debug_log_write(" ");
    debug_log_write(text);
    debug_log_write("=");
    debug_log_write_u32_inline(value);
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

static void rx_flush(void) {
    static const char hex[] = "0123456789ABCDEF";
    char line[RX_BUF_SIZE * 3u + 1u];
    uint32_t i;
    uint32_t pos;

    if (rx_len == 0U) {
        return;
    }

    pos = 0U;
    for (i = 0U; i < rx_len; ++i) {
        line[pos++] = hex[(rx_buf[i] >> 4) & 0xFU];
        line[pos++] = hex[rx_buf[i] & 0xFU];
        line[pos++] = ' ';
    }
    line[pos] = '\0';

    debug_log_write("[RX] n=");
    debug_log_write_u32_inline(rx_len);
    debug_log_write(" hex=");
    debug_log_write(line);

    pos = 0U;
    for (i = 0U; i < rx_len; ++i) {
        uint8_t b = rx_buf[i];
        line[pos++] = ((b >= 0x20U) && (b < 0x7FU)) ? (char)b : '.';
    }
    line[pos] = '\0';

    debug_log_write(" txt=");
    debug_log_write(line);
    debug_log_write("\r\n");

    rx_len = 0U;
}

/* ------------------------------------------------------------------ */
/*  Помощники по GPIO                                                  */
/* ------------------------------------------------------------------ */

#define GPIO_MODE_INPUT   0u
#define GPIO_MODE_OUTPUT  1u
#define GPIO_MODE_AF      2u

static inline void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode) {
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static inline void gpio_write(GPIO_TypeDef *port, uint32_t pin, uint32_t value) {
    port->BSRR = value ? (1u << pin) : (1u << (pin + 16u));
}

static inline uint32_t gpio_read(GPIO_TypeDef *port, uint32_t pin) {
    return (port->IDR >> pin) & 1u;
}

/* ------------------------------------------------------------------ */
/*  Питание FTDI (п.11)                                                */
/* ------------------------------------------------------------------ */

void FTDI_InitPins(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIOGEN;

    PWR->CR2 |= PWR_CR2_IOSV;   /* VDDIO2, иначе PG7/PG8 читаются как 0 */

    gpio_write(FTDI_RES_PORT, FTDI_RES_PIN, 0);
    gpio_set_mode(FTDI_RES_PORT, FTDI_RES_PIN, GPIO_MODE_OUTPUT);

    gpio_write(FTDI_PS_PORT, FTDI_PS_PIN, 0);
    gpio_set_mode(FTDI_PS_PORT, FTDI_PS_PIN, GPIO_MODE_OUTPUT);

    gpio_set_mode(FTDI_PSON_PORT, FTDI_PSON_PIN, GPIO_MODE_INPUT);
#if USE_VBUS_CHECK
    gpio_set_mode(USB_VBUS_PORT, USB_VBUS_PIN, GPIO_MODE_INPUT);
#endif
}

int FTDI_PowerOn(void) {
#if USE_VBUS_CHECK
    log_step_u32("[S05]", "usb_vbus", gpio_read(USB_VBUS_PORT, USB_VBUS_PIN));

    if (gpio_read(USB_VBUS_PORT, USB_VBUS_PIN) != 0u) {
        log_step("[S05]", "FAIL: ALARM_USB_VBUS");
        return 1;
    }
#endif

    log_step("[S06]", "PU_FTDI_PS = 1");
    gpio_write(FTDI_PS_PORT, FTDI_PS_PIN, 1);

    timebase_delay_ms_blocking(250u);

    log_step_u32("[S07]", "ftdi_pson", gpio_read(FTDI_PSON_PORT, FTDI_PSON_PIN));

    if (gpio_read(FTDI_PSON_PORT, FTDI_PSON_PIN) == 0u) {
        log_step("[S07]", "FAIL: PSON low, power off");
        gpio_write(FTDI_PS_PORT, FTDI_PS_PIN, 0);
        return 1;
    }

    log_step("[S08]", "PU_FTDI_RES = Z");
    gpio_set_mode(FTDI_RES_PORT, FTDI_RES_PIN, GPIO_MODE_INPUT);

    timebase_delay_ms_blocking(50u);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  USART1 (конфигурация 1:1 как в ftdi_test.c)                        */
/* ------------------------------------------------------------------ */

void UART1_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    gpio_set_mode(GPIOA, 9u,  GPIO_MODE_AF);
    gpio_set_mode(GPIOA, 10u, GPIO_MODE_AF);

    GPIOA->OSPEEDR |= (GPIO_OSPEEDR_OSPEED9 | GPIO_OSPEEDR_OSPEED10);

    GPIOA->AFR[1] &= ~((0xFu << GPIO_AFRH_AFSEL9_Pos) |
                       (0xFu << GPIO_AFRH_AFSEL10_Pos));
    GPIOA->AFR[1] |=  ((7u   << GPIO_AFRH_AFSEL9_Pos) |
                       (7u   << GPIO_AFRH_AFSEL10_Pos));

    USART1->CR3 = 0;
    USART1->CR2 = 0;
    USART1->BRR = FTDI_BAUD_BRR;
    USART1->CR1 = (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);

    log_step("[S10]", "wait REACK");
    while (!(USART1->ISR & USART_ISR_REACK));
    log_step("[S11]", "REACK ok");
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint32_t last_rx_ms;
    uint32_t last_beat_ms;

    clock_init();
    timebase_init();
    __enable_irq();

    (void)debug_log_init();
    log_step("[S01]", "ftdi RX test start");
    log_step("[S02]", "clock + timebase ok");

    FTDI_InitPins();
    log_step("[S03]", "FTDI_InitPins done");

    log_step("[S04]", "FTDI_PowerOn");
    if (FTDI_PowerOn() != 0) {
        log_step("[S09]", "STOP: FTDI_PowerOn failed");
        while (1);
    }
    log_step("[S09]", "FTDI_PowerOn ok");

    UART1_Init();

    log_step_u32("[S12]", "pa10_level", gpio_read(GPIOA, 10u));
    log_step("[S13]", "listening on PA10, send bytes from PC");

    last_rx_ms = timebase_millis();
    last_beat_ms = last_rx_ms;

    while (1) {
        uint32_t isr = USART1->ISR;
        uint32_t now;

        if ((isr & (USART_ISR_ORE | USART_ISR_FE |
                    USART_ISR_NE | USART_ISR_PE)) != 0U) {
            rx_last_err_isr = isr;
            ++rx_error_count;
            USART1->ICR = (USART_ICR_ORECF | USART_ICR_FECF |
                           USART_ICR_NECF | USART_ICR_PECF);
        }

        if ((isr & USART_ISR_RXNE) != 0U) {
            uint8_t byte = (uint8_t)(USART1->RDR & 0xFFU);

            ++rx_total;
            if (rx_len < RX_BUF_SIZE) {
                rx_buf[rx_len++] = byte;
            }
            last_rx_ms = timebase_millis();

            if (rx_len == RX_BUF_SIZE) {
                rx_flush();
            }
            continue;
        }

        now = timebase_millis();

        if ((rx_len != 0U) && ((now - last_rx_ms) >= RX_FLUSH_IDLE_MS)) {
            rx_flush();
        }

        if ((now - last_beat_ms) >= HEARTBEAT_MS) {
            last_beat_ms = now;

            debug_log_write("[S14] rx_total=");
            debug_log_write_u32_inline(rx_total);
            debug_log_write(" rx_errors=");
            debug_log_write_u32_inline(rx_error_count);
            debug_log_write(" pa10=");
            debug_log_write_u32_inline(gpio_read(GPIOA, 10u));
            debug_log_write("\r\n");

            if (rx_error_count != 0U) {
                log_hex32("[S14] last_err_isr", rx_last_err_isr);
            }
        }
    }
}
