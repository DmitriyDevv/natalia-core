#ifndef NATALIA_CORE_OBSERVE_H
#define NATALIA_CORE_OBSERVE_H

#include <stdint.h>

#include "ni_packet.h"
#include "state.h"

void observe_on_rtc_1hz(SystemContext *ctx);

void observe_start_session(SystemContext *ctx);
void observe_process_rtc_tick(SystemContext *ctx);
void observe_apply_kt(SystemContext *ctx, NiFormatType format,
                      const uint8_t *data, uint16_t length);
void observe_note_config_changed(SystemContext *ctx);
void observe_finish_session(SystemContext *ctx);

#endif //NATALIA_CORE_OBSERVE_H
