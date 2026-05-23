#include "can1_private.h"

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"
#include "clock.h"
#include "gpio.h"
#include "stm32l496xx.h"
#include "timebase.h"

#define CAN1_REQUIRED_PCLK1_HZ       (80000000UL)
#define CAN1_INIT_TIMEOUT_MS          (10UL)

/*
 * CAN1 bitrate configuration for PCLK1 = 80 MHz:
 *
 * Prescaler = 10
 * BS1       = 13 TQ
 * BS2       = 2 TQ
 * SJW       = 1 TQ
 *
 * Total TQ  = 1 + 13 + 2 = 16
 * Bitrate   = 80 MHz / 10 / 16 = 500 kbit/s
 * Sample point = (1 + 13) / 16 = 87.5%
 *
 * bxCAN stores values as "real value - 1".
 *
 * To use 1 Mbit/s with the same clock and segments,
 * change CAN1_BTR_BRP_VALUE from 9U to 4U:
 * 80 MHz / 5 / 16 = 1 Mbit/s.
 */
#define CAN1_BTR_BRP_VALUE           (9UL)   /* Prescaler = 10 */
#define CAN1_BTR_TS1_VALUE           (12UL)  /* BS1 = 13 TQ */
#define CAN1_BTR_TS2_VALUE           (1UL)   /* BS2 = 2 TQ */
#define CAN1_BTR_SJW_VALUE           (0UL)   /* SJW = 1 TQ */

#ifndef CAN_FMR_CAN2SB_Pos
#define CAN_FMR_CAN2SB_Pos           (8U)
#endif

#ifndef CAN_FMR_CAN2SB_Msk
#define CAN_FMR_CAN2SB_Msk           (0x3FUL << CAN_FMR_CAN2SB_Pos)
#endif

static BoardStatus can1_wait_msr(uint32_t mask, bool must_be_set)
{
    const uint32_t start_ms = timebase_millis();

    while (true) {
        const bool is_set = ((CAN1->MSR & mask) != 0U);

        if (is_set == must_be_set) {
            return BOARD_OK;
        }

        if (timebase_elapsed(start_ms, CAN1_INIT_TIMEOUT_MS)) {
            return BOARD_ERR_TIMEOUT;
        }
    }
}

static BoardStatus can1_configure_pins(void)
{
    const GpioConfig rx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_VERY_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    const GpioConfig tx_config = {
        .mode = GPIO_MODE_ALTERNATE,
        .pull = GPIO_PULL_NONE,
        .output_type = GPIO_OUTPUT_PUSH_PULL,
        .speed = GPIO_SPEED_VERY_HIGH,
        .initial_level = GPIO_LEVEL_HIGH
    };

    BoardStatus status;

    status = gpio_configure(BOARD_PIN_CAN1_RX, &rx_config);
    if (status != BOARD_OK) {
        return status;
    }

    return gpio_configure(BOARD_PIN_CAN1_TX, &tx_config);
}

static void can1_enable_clock_and_reset(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_CAN1EN;
    (void)RCC->APB1ENR1;

    RCC->APB1RSTR1 |= RCC_APB1RSTR1_CAN1RST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_CAN1RST;
}

static BoardStatus can1_enter_init_mode(void)
{
    CAN1->MCR &= ~CAN_MCR_SLEEP;

    if (can1_wait_msr(CAN_MSR_SLAK, false) != BOARD_OK) {
        return BOARD_ERR_TIMEOUT;
    }

    CAN1->MCR |= CAN_MCR_INRQ;

    return can1_wait_msr(CAN_MSR_INAK, true);
}

static BoardStatus can1_leave_init_mode(void)
{
    CAN1->MCR &= ~CAN_MCR_INRQ;

    return can1_wait_msr(CAN_MSR_INAK, false);
}

static void can1_configure_operating_mode(void)
{

    CAN1->MCR = CAN_MCR_INRQ |
                CAN_MCR_ABOM |
                CAN_MCR_TXFP |
                CAN_MCR_RFLM;
}

static void can1_configure_bit_timing(void)
{
    CAN1->BTR =
        (CAN1_BTR_SJW_VALUE << CAN_BTR_SJW_Pos) |
        (CAN1_BTR_TS2_VALUE << CAN_BTR_TS2_Pos) |
        (CAN1_BTR_TS1_VALUE << CAN_BTR_TS1_Pos) |
        (CAN1_BTR_BRP_VALUE << CAN_BTR_BRP_Pos);
}

static void can1_configure_accept_all_filter(void)
{

    CAN1->FMR |= CAN_FMR_FINIT;

    CAN1->FMR = (CAN1->FMR & ~CAN_FMR_CAN2SB_Msk) |
                (14UL << CAN_FMR_CAN2SB_Pos);

    CAN1->FA1R &= ~1UL; /* disable filter bank 0 during configuration */

    CAN1->FS1R |= 1UL;  /* 32-bit scale */
    CAN1->FM1R &= ~1UL; /* mask mode */
    CAN1->FFA1R &= ~1UL; /* FIFO0 */

    CAN1->sFilterRegister[0].FR1 = 0UL;
    CAN1->sFilterRegister[0].FR2 = 0UL;

    CAN1->FA1R |= 1UL;
    CAN1->FMR &= ~CAN_FMR_FINIT;
}

static void can1_clear_rx_flags(void)
{
    if ((CAN1->RF0R & CAN_RF0R_FULL0) != 0U) {
        CAN1->RF0R |= CAN_RF0R_FULL0;
    }

    if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0U) {
        CAN1->RF0R |= CAN_RF0R_FOVR0;
    }
}

static void can1_configure_interrupts(void)
{
    CAN1->IER = CAN_IER_TMEIE |
                CAN_IER_FMPIE0 |
                CAN_IER_ERRIE |
                CAN_IER_LECIE |
                CAN_IER_BOFIE |
                CAN_IER_EPVIE |
                CAN_IER_EWGIE;

    NVIC_ClearPendingIRQ(CAN1_TX_IRQn);
    NVIC_ClearPendingIRQ(CAN1_RX0_IRQn);
    NVIC_ClearPendingIRQ(CAN1_SCE_IRQn);

    NVIC_SetPriority(CAN1_SCE_IRQn, 2U);
    NVIC_SetPriority(CAN1_RX0_IRQn, 3U);
    NVIC_SetPriority(CAN1_TX_IRQn, 4U);

    NVIC_EnableIRQ(CAN1_SCE_IRQn);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);
    NVIC_EnableIRQ(CAN1_TX_IRQn);
}

BoardStatus can1_configure_hardware(void)
{
    BoardStatus status;

    if (clock_get_pclk1_hz() != CAN1_REQUIRED_PCLK1_HZ) {
        return BOARD_ERR_NOT_READY;
    }

    status = can1_configure_pins();
    if (status != BOARD_OK) {
        return status;
    }

    can1_enable_clock_and_reset();

    status = can1_enter_init_mode();
    if (status != BOARD_OK) {
        return status;
    }

    can1_configure_operating_mode();
    can1_configure_bit_timing();
    can1_configure_accept_all_filter();
    can1_clear_rx_flags();

    status = can1_leave_init_mode();
    if (status != BOARD_OK) {
        return status;
    }

    can1_configure_interrupts();

    return BOARD_OK;
}