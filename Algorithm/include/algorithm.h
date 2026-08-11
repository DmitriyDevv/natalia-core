#ifndef NATALIA_CORE_ALGORITHM_H
#define NATALIA_CORE_ALGORITHM_H

#include "state.h"

void algorithm_collect_hw_events(SystemContext *ctx);
void algorithm_poll(SystemContext *ctx);
void algorithm_process_events(SystemContext *ctx);

#endif // NATALIA_CORE_ALGORITHM_H
