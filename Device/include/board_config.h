#ifndef NATALIA_DEVICE_BOARD_CONFIG_H
#define NATALIA_DEVICE_BOARD_CONFIG_H

#include <stdint.h>

#define BOARD_CLOCK_SOURCE_HSI16       (0U)
#define BOARD_CLOCK_SOURCE_HSE_CRYSTAL (1U)
#define BOARD_CLOCK_SOURCE_HSE_BYPASS  (2U)

/*
 * Current development board: unmodified NUCLEO-L496ZG.
 *
 * Flight board with 8 MHz crystal:
 *   BOARD_CLOCK_SOURCE_HSE_CRYSTAL
 *
 * Modified Nucleo with ST-LINK MCO connected to HSE input:
 *   BOARD_CLOCK_SOURCE_HSE_BYPASS
 */
#define BOARD_CLOCK_SOURCE             BOARD_CLOCK_SOURCE_HSI16

#define BOARD_HSI16_FREQUENCY_HZ       (16000000UL)
#define BOARD_HSE_FREQUENCY_HZ         (8000000UL)

#define BOARD_SYSCLK_HZ                (80000000UL)
#define BOARD_HCLK_HZ                  (80000000UL)
#define BOARD_PCLK1_HZ                 (80000000UL)
#define BOARD_PCLK2_HZ                 (80000000UL)

#endif /* NATALIA_DEVICE_BOARD_CONFIG_H */