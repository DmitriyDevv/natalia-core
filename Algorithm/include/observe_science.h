#ifndef NATALIA_CORE_OBSERVE_SCIENCE_H
#define NATALIA_CORE_OBSERVE_SCIENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "instrument_time.h"
#include "ni_format.h"
#include "ni_stream.h"

#define OBSERVE_SCIENCE_TELEMETRY_PERIOD_S 20U

typedef enum {
    OBSERVE_SCIENCE_OK = 0,
    OBSERVE_SCIENCE_NO_ROOM,
    OBSERVE_SCIENCE_INVALID_ARG
} ObserveScienceStatus;

typedef enum {
    OBSERVE_EVENTS_OFF = 0,
    OBSERVE_EVENTS_ALWAYS = 1,
    OBSERVE_EVENTS_OUTSIDE_BELT_BCSAT = 2,
    OBSERVE_EVENTS_OUTSIDE_BELT_BMSAT = 3,
    OBSERVE_EVENTS_OUTSIDE_BELT_B = 4,
    OBSERVE_EVENTS_OUTSIDE_BELTS_BL = 5,
    OBSERVE_EVENTS_AC1_BELOW_MAX = 6
} ObserveEventsMode;

typedef enum {
    OBSERVE_SPECTRUM_OFF = 0,
    OBSERVE_SPECTRUM_1 = 1,
    OBSERVE_SPECTRUM_2 = 2
} ObserveSpectrumMode;

typedef struct {
    uint8_t events_mode;
    uint8_t events_nmax;
    uint8_t spectrum_mode;
    uint16_t spectrum_bins;
} ObserveScienceParams;

typedef struct {
    int16_t belt_bmin;
    int16_t belt_lmin;
    int16_t belt_lmax;
    uint16_t ac1_rate_max;
} ObserveScienceLimits;

typedef struct {
    NiCounters counters;
    uint32_t rtc_seconds;
} ObserveSecondMark;

typedef struct {
    ObserveScienceParams params;
    ObserveScienceParams pending_params;
    ObserveScienceLimits limits;
    uint32_t format_number;
    uint32_t current_rtc_seconds;
    uint32_t seconds_since_start;
    uint8_t observe_mode_number;
    bool started;
    bool params_pending;
    bool final_requested;
    bool magfield_valid;
    bool bcsat_inside_inner_belt;
    bool bmsat_inside_inner_belt;
    bool mcilwain_valid;
    int16_t mcilwain_b;
    int16_t mcilwain_l;
    bool ac1_valid;
    bool events_wait_kt;
    bool kt_received;
    uint16_t last_ac1;
    size_t event_count;
    NiEventRecord events[NI_FORMAT_EVENTS_MAX];
    uint8_t histogram[NI_FORMAT_SPECTRUM_BINS_MAX];
    uint8_t scratch[NI_FORMAT_MAX_BYTES];
} ObserveScience;

bool observe_science_decode_params(uint16_t raw_params, ObserveScienceParams* params);

void observe_science_begin(ObserveScience* science,
                           const ObserveScienceParams* params,
                           const ObserveScienceLimits* limits);
void observe_science_request_params(ObserveScience* science, const ObserveScienceParams* params);
void observe_science_request_final(ObserveScience* science);
void observe_science_set_events_wait_kt(ObserveScience* science, bool wait);
bool observe_science_second_needs_telemetry(const ObserveScience* science);

ObserveScienceStatus observe_science_on_event(ObserveScience* science, NiStream* stream,
                                              const NiEventRecord* event);
ObserveScienceStatus observe_science_on_second(ObserveScience* science, NiStream* stream,
                                               const ObserveSecondMark* mark,
                                               const NiTelemetry* telemetry);
ObserveScienceStatus observe_science_on_sync_orbit(ObserveScience* science, NiStream* stream,
                                                   const InstrumentTime* received_at,
                                                   const uint8_t* kt, size_t kt_length);
ObserveScienceStatus observe_science_on_geomagnetic(ObserveScience* science, NiStream* stream,
                                                    uint32_t rtc_seconds,
                                                    const uint8_t* kt, size_t kt_length);
ObserveScienceStatus observe_science_on_mcilwain(ObserveScience* science, NiStream* stream,
                                                 uint32_t rtc_seconds,
                                                 const uint8_t* kt, size_t kt_length);
ObserveScienceStatus observe_science_flush_events(ObserveScience* science, NiStream* stream);

uint8_t observe_science_mode_number(const ObserveScience* science);
uint32_t observe_science_format_number(const ObserveScience* science);

#endif /* NATALIA_CORE_OBSERVE_SCIENCE_H */
