/*
 * Standalone FTDI/USART1 smoke test. No project code.
 *
 * Runs on the reset-default clock: MSI 4 MHz, SYSCLK = MSI, all prescalers /1,
 * so PCLK2 = 4 MHz. USART1 default clock source = PCLK2. No clock setup needed.
 *
 * Pins:
 *   PA9  = USART1_TX  (AF7)
 *   PC6  = PU_FTDI_PS  (output, high = power on)
 *   PC7  = PU_FTDI_RES (output, active low reset -> high = run)
 *
 * Baud: 9600, 8N1, no flow control.  BRR = 4 MHz / 9600 = 417.
 */

#include <stdint.h>

#include "stm32l496xx.h"

#define SMOKE_PCLK2_HZ 4000000UL
#define SMOKE_BAUD     9600UL

static void crude_delay(volatile uint32_t loops) {
    while (loops-- != 0U) {
        __asm volatile("nop");
    }
}

static void ftdi_power_pins_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;

    /* PC6, PC7 -> general purpose output (01) */
    GPIOC->MODER &= ~(GPIO_MODER_MODE6 | GPIO_MODER_MODE7);
    GPIOC->MODER |= (GPIO_MODER_MODE6_0 | GPIO_MODER_MODE7_0);

    /* PU_FTDI_PS = high (power on) */
    GPIOC->BSRR = GPIO_BSRR_BS6;

    /* PU_FTDI_RES: assert reset low, hold, then release high (run) */
    GPIOC->BSRR = GPIO_BSRR_BR7;
    crude_delay(200000U);
    GPIOC->BSRR = GPIO_BSRR_BS7;

    crude_delay(400000U);
}

static void usart1_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    /* PA9 -> alternate function (10) */
    GPIOA->MODER &= ~GPIO_MODER_MODE9;
    GPIOA->MODER |= GPIO_MODER_MODE9_1;

    /* PA9 -> AF7 (USART1) */
    GPIOA->AFR[1] &= ~(0xFUL << GPIO_AFRH_AFSEL9_Pos);
    GPIOA->AFR[1] |= (7UL << GPIO_AFRH_AFSEL9_Pos);

    /* USART1 default clock source (PCLK2), 8N1, no flow control */
    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    USART1->BRR = (SMOKE_PCLK2_HZ + (SMOKE_BAUD / 2U)) / SMOKE_BAUD;
    USART1->CR1 = USART_CR1_TE;
    USART1->CR1 |= USART_CR1_UE;
}

static void usart1_write_byte(uint8_t byte) {
    while ((USART1->ISR & USART_ISR_TXE) == 0U) {
    }
    USART1->TDR = byte;
}

static void usart1_write_string(const char *s) {
    while (*s != '\0') {
        usart1_write_byte((uint8_t)*s);
        ++s;
    }
}

int main(void) {
    uint32_t counter = 0U;

    ftdi_power_pins_init();
    usart1_init();

    usart1_write_string("\r\nFTDI SMOKE TEST ALIVE\r\n");

    for (;;) {
        usart1_write_string("smoke ");
        usart1_write_byte((uint8_t)('0' + (counter % 10U)));
        usart1_write_string("\r\n");

        ++counter;
        crude_delay(2000000U);
    }
}
