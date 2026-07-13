#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ni_packet.h"
#include "observe.h"
#include "state.h"
#include "tlm_staging.h"

#define OBSERVE_PARAMS_SPECTRUM_1 (0x40U)

static void setup_active(SystemContext* ctx, uint16_t params) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_OBSERVE;
    ctx->observe.stage = OBSERVE_STAGE_ACTIVE;
    ctx->observe.observe_params = params;
    observe_start_session(ctx);
}

static void first_tick_is_telemetry(void) {
    SystemContext ctx;

    setup_active(&ctx, OBSERVE_PARAMS_SPECTRUM_1);

    assert(ctx.observe_session_id == 1U);
    assert(ctx.observe.spectrum_mode == 1U);
    assert(ctx.observe.first_tick);

    observe_process_rtc_tick(&ctx);

    assert(ctx.observe.last_emitted_format == (uint8_t)NI_FORMAT_TELEMETRY);
    assert(ctx.observe.format_number == 1U);
    assert(!ctx.observe.first_tick);
}

static void counters_then_spectrum(void) {
    SystemContext ctx;

    setup_active(&ctx, OBSERVE_PARAMS_SPECTRUM_1);

    observe_process_rtc_tick(&ctx);
    observe_process_rtc_tick(&ctx);

    assert(ctx.observe.last_emitted_format == (uint8_t)NI_FORMAT_SPECTRUM_1);
    assert(ctx.observe.format_number == 3U);
    assert(ctx.observe.seconds_elapsed == 1U);
}

static void telemetry_every_20s(void) {
    SystemContext ctx;
    int i;

    setup_active(&ctx, 0x00U);

    observe_process_rtc_tick(&ctx);
    for (i = 0; i < 19; ++i) {
        observe_process_rtc_tick(&ctx);
    }
    assert(ctx.observe.seconds_elapsed == 19U);

    observe_process_rtc_tick(&ctx);
    assert(ctx.observe.seconds_elapsed == 20U);
    assert(ctx.observe.last_emitted_format == (uint8_t)NI_FORMAT_TELEMETRY);
}

static void config_change_schedules_telemetry(void) {
    SystemContext ctx;

    setup_active(&ctx, 0x00U);
    observe_process_rtc_tick(&ctx);

    observe_note_config_changed(&ctx);
    assert(ctx.observe.observe_mode_number == 1U);
    assert(ctx.observe.telem_pending);

    observe_process_rtc_tick(&ctx);
    assert(ctx.observe.last_emitted_format == (uint8_t)NI_FORMAT_TELEMETRY);
    assert(!ctx.observe.telem_pending);
}

static void kt_payload_saved_verbatim(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[24];
    uint8_t slot;
    size_t i;

    setup_active(&ctx, 0x00U);

    for (i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(0xB0U + i);
    }

    slot = tlm_staging_put(payload, (uint16_t)sizeof(payload));

    memset(&event, 0, sizeof(event));
    event.type = EVENT_TLM_MAGFIELD;
    event.tlm_slot = slot;

    (void)handle_event(&ctx, &event);

    assert(ctx.observe.kt_geomagnetic.valid);
    assert(ctx.observe.kt_geomagnetic.length == sizeof(payload));
    assert(memcmp(ctx.observe.kt_geomagnetic.data, payload, sizeof(payload)) == 0);
    assert(ctx.observe.last_emitted_format == (uint8_t)NI_FORMAT_GEOMAGNETIC);
}

int main(void) {
    first_tick_is_telemetry();
    counters_then_spectrum();
    telemetry_every_20s();
    config_change_schedules_telemetry();
    kt_payload_saved_verbatim();

    return 0;
}
