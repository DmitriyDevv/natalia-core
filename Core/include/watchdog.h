#ifndef NATALIA_CORE_WATCHDOG_H
#define NATALIA_CORE_WATCHDOG_H

#include "status.h"

/* Independent watchdog (IWDG). watchdog_init starts it just before the main
 * super-loop; watchdog_kick must be called at least once per loop iteration.
 * A missed kick (an in-iteration hang) resets the MCU. When
 * NATALIA_ENABLE_WATCHDOG is not defined both functions are no-ops. */
BoardStatus watchdog_init(void);
void watchdog_kick(void);

#endif /* NATALIA_CORE_WATCHDOG_H */
