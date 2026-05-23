#ifndef NATALIA_CORE_CLOCK_H
#define NATALIA_CORE_CLOCK_H

#include <stdint.h>

#include "status.h"


BoardStatus clock_init(void);

uint32_t clock_get_sysclk_hz(void);
uint32_t clock_get_hclk_hz(void);
uint32_t clock_get_pclk1_hz(void);
uint32_t clock_get_pclk2_hz(void);

#endif /* NATALIA_CORE_CLOCK_H */
