#include "observe_science.h"

#include <string.h>

_Static_assert(sizeof(float) == 4U, "KT floats are 32-bit IEEE 754");

#define OBSERVE_SCIENCE_AMP_BITS             16U
#define OBSERVE_SCIENCE_BIN_MAX              0xFFU
#define OBSERVE_SCIENCE_TESLA_SQ_TO_GS1000_SQ 1.0e14f
#define OBSERVE_SCIENCE_KT_BMSAT_OFFSET      0U
#define OBSERVE_SCIENCE_KT_BCSAT_OFFSET      12U
#define OBSERVE_SCIENCE_KT_L_OFFSET          20U
#define OBSERVE_SCIENCE_KT_B_OFFSET          22U

static const uint8_t observe_science_nmax_table[] = {0U, 1U, 10U, 20U, 50U, 100U};
static const uint16_t observe_science_bins_table[] = {0U, 256U, 512U, 1024U, 2048U};

static uint16_t observe_science_read_le_u16(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static float observe_science_read_le_float(const uint8_t* bytes) {
    uint32_t raw = (uint32_t)bytes[0] |
                   ((uint32_t)bytes[1] << 8) |
                   ((uint32_t)bytes[2] << 16) |
                   ((uint32_t)bytes[3] << 24);
    float value;

    (void)memcpy(&value, &raw, sizeof(value));

    return value;
}

static bool observe_science_field_inside_belt(const uint8_t* xyz, int16_t bmin) {
    float x = observe_science_read_le_float(&xyz[0]);
    float y = observe_science_read_le_float(&xyz[4]);
    float z = observe_science_read_le_float(&xyz[8]);
    float magnitude_sq = (x * x) + (y * y) + (z * z);
    float limit = (float)bmin;

    if (bmin < 0) {
        return false;
    }

    return (magnitude_sq * OBSERVE_SCIENCE_TESLA_SQ_TO_GS1000_SQ) <= (limit * limit);
}

static uint32_t observe_science_bin_shift(uint16_t bins) {
    uint32_t bits = 0U;

    while ((bins > 1U) && (bits < OBSERVE_SCIENCE_AMP_BITS)) {
        bins = (uint16_t)(bins >> 1);
        ++bits;
    }

    return OBSERVE_SCIENCE_AMP_BITS - bits;
}

static NiFormatStamp observe_science_stamp(const ObserveScience* science, uint32_t rtc_seconds) {
    NiFormatStamp stamp;

    stamp.observe_mode_number = science->observe_mode_number;
    stamp.format_number = science->format_number;
    stamp.rtc_seconds = rtc_seconds;

    return stamp;
}

static void observe_science_commit(ObserveScience* science, NiStream* stream, size_t words) {
    if ((words > 0U) && ni_stream_append(stream, science->scratch, words)) {
        ++science->format_number;
    }
}

static bool observe_science_events_enabled(const ObserveScience* science) {
    return (science->params.events_mode != (uint8_t)OBSERVE_EVENTS_OFF) &&
           (science->params.events_nmax > 0U);
}

static bool observe_science_events_allowed(const ObserveScience* science) {
    const ObserveScienceLimits* limits = &science->limits;

    switch (science->params.events_mode) {
    case OBSERVE_EVENTS_ALWAYS:
        return true;

    case OBSERVE_EVENTS_OUTSIDE_BELT_BCSAT:
        return science->magfield_valid && !science->bcsat_inside_inner_belt;

    case OBSERVE_EVENTS_OUTSIDE_BELT_BMSAT:
        return science->magfield_valid && !science->bmsat_inside_inner_belt;

    case OBSERVE_EVENTS_OUTSIDE_BELT_B:
        return science->mcilwain_valid && (science->mcilwain_b > limits->belt_bmin);

    case OBSERVE_EVENTS_OUTSIDE_BELTS_BL:
        return science->mcilwain_valid &&
               (science->mcilwain_b > limits->belt_bmin) &&
               ((science->mcilwain_l < limits->belt_lmin) ||
                (science->mcilwain_l > limits->belt_lmax));

    case OBSERVE_EVENTS_AC1_BELOW_MAX:
        return science->ac1_valid && (science->last_ac1 < limits->ac1_rate_max);

    default:
        return false;
    }
}

static void observe_science_add_to_histogram(ObserveScience* science, uint16_t amplitude) {
    uint32_t bin;

    if ((science->params.spectrum_mode == (uint8_t)OBSERVE_SPECTRUM_OFF) ||
        (science->params.spectrum_bins == 0U)) {
        return;
    }

    bin = (uint32_t)amplitude >> observe_science_bin_shift(science->params.spectrum_bins);

    if ((bin < science->params.spectrum_bins) &&
        (science->histogram[bin] < OBSERVE_SCIENCE_BIN_MAX)) {
        ++science->histogram[bin];
    }
}

static size_t observe_science_spectrum_words(const ObserveScience* science) {
    switch (science->params.spectrum_mode) {
    case OBSERVE_SPECTRUM_1:
        return ni_format_spectrum1_words(science->params.spectrum_bins);

    case OBSERVE_SPECTRUM_2:
        return ni_format_spectrum2_words(science->histogram);

    default:
        return 0U;
    }
}

static void observe_science_emit_events(ObserveScience* science, NiStream* stream,
                                        uint32_t rtc_seconds) {
    NiFormatStamp stamp;
    size_t words;

    if (science->event_count == 0U) {
        return;
    }

    stamp = observe_science_stamp(science, rtc_seconds);
    words = ni_format_build_events(&stamp, science->events, science->event_count,
                                   science->scratch, sizeof(science->scratch));
    observe_science_commit(science, stream, words);
    science->event_count = 0U;
}

static void observe_science_emit_spectrum(ObserveScience* science, NiStream* stream,
                                          uint32_t rtc_seconds) {
    NiFormatStamp stamp = observe_science_stamp(science, rtc_seconds);
    size_t words = 0U;

    if (science->params.spectrum_mode == (uint8_t)OBSERVE_SPECTRUM_1) {
        words = ni_format_build_spectrum1(&stamp, science->histogram,
                                          science->params.spectrum_bins,
                                          science->scratch, sizeof(science->scratch));
    } else if (science->params.spectrum_mode == (uint8_t)OBSERVE_SPECTRUM_2) {
        words = ni_format_build_spectrum2(&stamp, science->histogram,
                                          science->scratch, sizeof(science->scratch));
    }

    observe_science_commit(science, stream, words);
}

static void observe_science_apply_pending_params(ObserveScience* science) {
    if (!science->params_pending) {
        return;
    }

    science->params = science->pending_params;
    science->params_pending = false;
    ++science->observe_mode_number;
}

bool observe_science_decode_params(uint16_t raw_params, ObserveScienceParams* params) {
    uint16_t events_mode = raw_params & 0x0007U;
    uint16_t nmax_code = (raw_params >> 3) & 0x0007U;
    uint16_t spectrum_mode = (raw_params >> 6) & 0x0003U;
    uint16_t bins_code = (raw_params >> 8) & 0x0007U;

    if (params == NULL) {
        return false;
    }

    if ((events_mode > (uint16_t)OBSERVE_EVENTS_AC1_BELOW_MAX) || (nmax_code > 5U) ||
        (spectrum_mode > (uint16_t)OBSERVE_SPECTRUM_2) || (bins_code > 4U) ||
        ((raw_params & (1U << 11)) != 0U)) {
        return false;
    }

    if ((events_mode == 0U) != (nmax_code == 0U)) {
        return false;
    }

    if (((spectrum_mode == (uint16_t)OBSERVE_SPECTRUM_1) && (bins_code == 0U)) ||
        ((spectrum_mode != (uint16_t)OBSERVE_SPECTRUM_1) && (bins_code != 0U))) {
        return false;
    }

    params->events_mode = (uint8_t)events_mode;
    params->events_nmax = observe_science_nmax_table[nmax_code];
    params->spectrum_mode = (uint8_t)spectrum_mode;

    if (spectrum_mode == (uint16_t)OBSERVE_SPECTRUM_1) {
        params->spectrum_bins = observe_science_bins_table[bins_code];
    } else if (spectrum_mode == (uint16_t)OBSERVE_SPECTRUM_2) {
        params->spectrum_bins = (uint16_t)NI_FORMAT_SPECTRUM_BINS_MAX;
    } else {
        params->spectrum_bins = 0U;
    }

    return true;
}

void observe_science_begin(ObserveScience* science,
                           const ObserveScienceParams* params,
                           const ObserveScienceLimits* limits) {
    if ((science == NULL) || (params == NULL) || (limits == NULL)) {
        return;
    }

    (void)memset(science, 0, sizeof(*science));
    science->params = *params;
    science->limits = *limits;
}

void observe_science_request_params(ObserveScience* science, const ObserveScienceParams* params) {
    if ((science == NULL) || (params == NULL)) {
        return;
    }

    science->pending_params = *params;
    science->params_pending = true;
}

void observe_science_request_final(ObserveScience* science) {
    if (science != NULL) {
        science->final_requested = true;
    }
}

bool observe_science_second_needs_telemetry(const ObserveScience* science) {
    if (science == NULL) {
        return false;
    }

    if (!science->started || science->params_pending || science->final_requested) {
        return true;
    }

    return ((science->seconds_since_start + 1U) % OBSERVE_SCIENCE_TELEMETRY_PERIOD_S) == 0U;
}

ObserveScienceStatus observe_science_on_event(ObserveScience* science, NiStream* stream,
                                              const NiEventRecord* event) {
    bool record;

    if ((science == NULL) || (stream == NULL) || (event == NULL)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    if (!science->started) {
        return OBSERVE_SCIENCE_OK;
    }

    record = observe_science_events_enabled(science) && observe_science_events_allowed(science);

    if (record && ((science->event_count + 1U) >= science->params.events_nmax) &&
        (ni_format_events_words(science->event_count + 1U) > ni_stream_free_words(stream))) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    observe_science_add_to_histogram(science, event->amp_d);

    if (record) {
        science->events[science->event_count] = *event;
        ++science->event_count;

        if (science->event_count >= science->params.events_nmax) {
            observe_science_emit_events(science, stream, science->current_rtc_seconds);
        }
    }

    return OBSERVE_SCIENCE_OK;
}

ObserveScienceStatus observe_science_on_second(ObserveScience* science, NiStream* stream,
                                               const ObserveSecondMark* mark,
                                               const NiTelemetry* telemetry) {
    NiFormatStamp stamp;
    size_t needed;
    size_t words;
    bool telemetry_due;

    if ((science == NULL) || (stream == NULL) || (mark == NULL)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    telemetry_due = observe_science_second_needs_telemetry(science);
    if (telemetry_due && (telemetry == NULL)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    needed = telemetry_due ? NI_FORMAT_TELEMETRY_WORDS : 0U;
    if (science->started) {
        needed += ni_format_events_words(science->event_count);
        needed += NI_FORMAT_COUNTERS_WORDS;
        needed += observe_science_spectrum_words(science);
    }

    if (needed > ni_stream_free_words(stream)) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    if (science->started) {
        observe_science_emit_events(science, stream, mark->rtc_seconds);

        stamp = observe_science_stamp(science, mark->rtc_seconds);
        words = ni_format_build_counters(&stamp, &mark->counters,
                                         science->scratch, sizeof(science->scratch));
        observe_science_commit(science, stream, words);

        observe_science_emit_spectrum(science, stream, mark->rtc_seconds);

        science->last_ac1 = mark->counters.n_ac1;
        science->ac1_valid = true;
        ++science->seconds_since_start;
    } else {
        science->started = true;
        science->seconds_since_start = 0U;
    }

    (void)memset(science->histogram, 0, sizeof(science->histogram));
    science->event_count = 0U;
    observe_science_apply_pending_params(science);

    if (telemetry_due) {
        stamp = observe_science_stamp(science, mark->rtc_seconds);
        words = ni_format_build_telemetry(&stamp, telemetry,
                                          science->scratch, sizeof(science->scratch));
        observe_science_commit(science, stream, words);
    }

    science->current_rtc_seconds = mark->rtc_seconds;

    return OBSERVE_SCIENCE_OK;
}

ObserveScienceStatus observe_science_on_sync_orbit(ObserveScience* science, NiStream* stream,
                                                   const InstrumentTime* received_at,
                                                   const uint8_t* kt, size_t kt_length) {
    NiFormatStamp stamp;
    size_t words;

    if ((science == NULL) || (stream == NULL) || (received_at == NULL) || (kt == NULL) ||
        (kt_length != NI_KT_SYNC_ORBIT_BYTES)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    if (!science->started) {
        return OBSERVE_SCIENCE_OK;
    }

    if (NI_FORMAT_SYNC_ORBIT_WORDS > ni_stream_free_words(stream)) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    stamp = observe_science_stamp(science, received_at->seconds);
    words = ni_format_build_sync_orbit(&stamp, received_at, kt, kt_length,
                                       science->scratch, sizeof(science->scratch));
    observe_science_commit(science, stream, words);

    return OBSERVE_SCIENCE_OK;
}

ObserveScienceStatus observe_science_on_geomagnetic(ObserveScience* science, NiStream* stream,
                                                    uint32_t rtc_seconds,
                                                    const uint8_t* kt, size_t kt_length) {
    NiFormatStamp stamp;
    size_t words;

    if ((science == NULL) || (stream == NULL) || (kt == NULL) ||
        (kt_length != NI_KT_GEOMAGNETIC_BYTES)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    if (science->started && (NI_FORMAT_GEOMAGNETIC_WORDS > ni_stream_free_words(stream))) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    science->bmsat_inside_inner_belt =
        observe_science_field_inside_belt(&kt[OBSERVE_SCIENCE_KT_BMSAT_OFFSET],
                                          science->limits.belt_bmin);
    science->bcsat_inside_inner_belt =
        observe_science_field_inside_belt(&kt[OBSERVE_SCIENCE_KT_BCSAT_OFFSET],
                                          science->limits.belt_bmin);
    science->magfield_valid = true;

    if (science->started) {
        stamp = observe_science_stamp(science, rtc_seconds);
        words = ni_format_build_geomagnetic(&stamp, kt, kt_length,
                                            science->scratch, sizeof(science->scratch));
        observe_science_commit(science, stream, words);
    }

    return OBSERVE_SCIENCE_OK;
}

ObserveScienceStatus observe_science_on_mcilwain(ObserveScience* science, NiStream* stream,
                                                 uint32_t rtc_seconds,
                                                 const uint8_t* kt, size_t kt_length) {
    NiFormatStamp stamp;
    size_t words;

    if ((science == NULL) || (stream == NULL) || (kt == NULL) ||
        (kt_length != NI_KT_MCILWAIN_BYTES)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    if (science->started && (NI_FORMAT_MCILWAIN_WORDS > ni_stream_free_words(stream))) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    science->mcilwain_l = (int16_t)observe_science_read_le_u16(&kt[OBSERVE_SCIENCE_KT_L_OFFSET]);
    science->mcilwain_b = (int16_t)observe_science_read_le_u16(&kt[OBSERVE_SCIENCE_KT_B_OFFSET]);
    science->mcilwain_valid = true;

    if (science->started) {
        stamp = observe_science_stamp(science, rtc_seconds);
        words = ni_format_build_mcilwain(&stamp, kt, kt_length,
                                         science->scratch, sizeof(science->scratch));
        observe_science_commit(science, stream, words);
    }

    return OBSERVE_SCIENCE_OK;
}

ObserveScienceStatus observe_science_flush_events(ObserveScience* science, NiStream* stream) {
    if ((science == NULL) || (stream == NULL)) {
        return OBSERVE_SCIENCE_INVALID_ARG;
    }

    if (ni_format_events_words(science->event_count) > ni_stream_free_words(stream)) {
        return OBSERVE_SCIENCE_NO_ROOM;
    }

    observe_science_emit_events(science, stream, science->current_rtc_seconds);

    return OBSERVE_SCIENCE_OK;
}

uint8_t observe_science_mode_number(const ObserveScience* science) {
    return (science == NULL) ? 0U : science->observe_mode_number;
}

uint32_t observe_science_format_number(const ObserveScience* science) {
    return (science == NULL) ? 0U : science->format_number;
}
