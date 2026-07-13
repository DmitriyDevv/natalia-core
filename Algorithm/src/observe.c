#include "observe.h"

#include <string.h>

#include "event_queue.h"
#include "ni_packet.h"

void observe_on_rtc_1hz(SystemContext *ctx) {
    (void)ctx;
    (void)system_event_queue_push_back_type(EVENT_RTC_1HZ);
}

static void observe_emit_format(SystemContext *ctx, NiFormatType format) {
    ni_packet_write_format(ctx, format);
    ++ctx->observe.format_number;
    ctx->observe.last_emitted_format = (uint8_t)format;
}

void observe_start_session(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;
    uint16_t params = observe->observe_params;

    ++ctx->observe_session_id;
    observe->observe_mode_number = 0U;
    observe->format_number = 0U;
    observe->seconds_elapsed = 0U;
    observe->first_tick = true;
    observe->telem_pending = false;
    observe->last_emitted_format = 0xFFU;

    observe->events_mode = (uint8_t)(params & 0x7U);
    observe->events_nmax_sel = (uint8_t)((params >> 3) & 0x7U);
    observe->spectrum_mode = (uint8_t)((params >> 6) & 0x3U);
    observe->spectrum_nhist_sel = (uint8_t)((params >> 8) & 0x7U);

    observe->kt_sync_orbit_attitude.valid = false;
    observe->kt_geomagnetic.valid = false;
    observe->kt_mcilwain.valid = false;

    ni_packet_session_begin(ctx);
}

void observe_process_rtc_tick(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;

    if (observe->stage != OBSERVE_STAGE_ACTIVE) {
        return;
    }

    if (observe->first_tick) {
        observe_emit_format(ctx, NI_FORMAT_TELEMETRY);
        observe->first_tick = false;
        observe->seconds_elapsed = 0U;
        return;
    }

    observe_emit_format(ctx, NI_FORMAT_COUNTERS);

    if (observe->spectrum_mode == 1U) {
        observe_emit_format(ctx, NI_FORMAT_SPECTRUM_1);
    } else if (observe->spectrum_mode == 2U) {
        observe_emit_format(ctx, NI_FORMAT_SPECTRUM_2);
    }

    ++observe->seconds_elapsed;

    if (observe->telem_pending) {
        observe_emit_format(ctx, NI_FORMAT_TELEMETRY);
        observe->telem_pending = false;
    } else if ((observe->seconds_elapsed % 20U) == 0U) {
        observe_emit_format(ctx, NI_FORMAT_TELEMETRY);
    }
}

void observe_apply_kt(SystemContext *ctx, NiFormatType format,
                      const uint8_t *data, uint16_t length) {
    ObserveContext *observe = &ctx->observe;
    KtLatch *latch;

    if (observe->stage != OBSERVE_STAGE_ACTIVE) {
        return;
    }

    switch (format) {
    case NI_FORMAT_SYNC_ORBIT_ATTITUDE:
        latch = &observe->kt_sync_orbit_attitude;
        break;
    case NI_FORMAT_GEOMAGNETIC:
        latch = &observe->kt_geomagnetic;
        break;
    case NI_FORMAT_MCILWAIN:
        latch = &observe->kt_mcilwain;
        break;
    default:
        return;
    }

    if (length > (uint16_t)TLM_PAYLOAD_MAX) {
        length = (uint16_t)TLM_PAYLOAD_MAX;
    }

    if ((data != NULL) && (length > 0U)) {
        (void)memcpy(latch->data, data, length);
    }

    latch->length = length;
    latch->valid = true;

    observe_emit_format(ctx, format);
}

void observe_note_config_changed(SystemContext *ctx) {
    ++ctx->observe.observe_mode_number;
    ctx->observe.telem_pending = true;
}

void observe_finish_session(SystemContext *ctx) {
    observe_emit_format(ctx, NI_FORMAT_TELEMETRY);
    ni_packet_session_end(ctx);
}
