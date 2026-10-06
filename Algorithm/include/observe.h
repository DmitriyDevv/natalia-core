#ifndef NATALIA_CORE_OBSERVE_H
#define NATALIA_CORE_OBSERVE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

#define OBSERVE_RECORDS_PER_POLL 64U
#define OBSERVE_RECORDS_PER_WRITER_POLL 16U
#define OBSERVE_FINISH_TIMEOUT_S 3U

BoardStatus observe_begin(SystemContext *ctx);
void observe_poll(SystemContext *ctx);
void observe_on_rtc_tick(SystemContext *ctx);

bool observe_request_params(SystemContext *ctx, uint16_t raw_params);
void observe_accept_kt(SystemContext *ctx, ObserveKtType type,
                       const uint8_t *data, uint16_t length);
void observe_request_final(SystemContext *ctx);
void observe_abort(SystemContext *ctx);

uint8_t observe_hardware_config(const SystemContext *ctx);

#endif //NATALIA_CORE_OBSERVE_H
