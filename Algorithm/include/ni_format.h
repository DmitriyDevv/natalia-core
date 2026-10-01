#ifndef NATALIA_CORE_NI_FORMAT_H
#define NATALIA_CORE_NI_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "instrument_time.h"

#define NI_FORMAT_MARK                 0xFEFAU
#define NI_FORMAT_CLASS_CODE           0x0FU
#define NI_FORMAT_HEADER_WORDS         9U
#define NI_FORMAT_EVENT_WORDS          4U
#define NI_FORMAT_EVENTS_MAX           100U
#define NI_FORMAT_EVENTS_MAX_WORDS     (NI_FORMAT_HEADER_WORDS + (NI_FORMAT_EVENTS_MAX * NI_FORMAT_EVENT_WORDS) + 1U)
#define NI_FORMAT_COUNTERS_WORDS       15U
#define NI_FORMAT_TELEMETRY_WORDS      30U
#define NI_FORMAT_SYNC_ORBIT_WORDS     76U
#define NI_FORMAT_GEOMAGNETIC_WORDS    48U
#define NI_FORMAT_MCILWAIN_WORDS       22U
#define NI_FORMAT_SPECTRUM_BINS_MAX    2048U
#define NI_FORMAT_SPECTRUM2_GROUPS     8U
#define NI_FORMAT_SPECTRUM2_GROUP_BINS 256U
#define NI_FORMAT_SPECTRUM2_MIN_WORDS  (NI_FORMAT_HEADER_WORDS + NI_FORMAT_SPECTRUM2_GROUPS + 1U)
#define NI_FORMAT_SPECTRUM2_MAX_WORDS  (NI_FORMAT_SPECTRUM2_MIN_WORDS + NI_FORMAT_SPECTRUM_BINS_MAX)
#define NI_FORMAT_MAX_WORDS            NI_FORMAT_SPECTRUM2_MAX_WORDS
#define NI_FORMAT_MAX_BYTES            (NI_FORMAT_MAX_WORDS * 2U)

#define NI_KT_SYNC_ORBIT_BYTES         125U
#define NI_KT_GEOMAGNETIC_BYTES        76U
#define NI_KT_MCILWAIN_BYTES           24U

typedef enum {
    NI_FORMAT_EVENTS              = 0x00U,
    NI_FORMAT_COUNTERS            = 0x01U,
    NI_FORMAT_SPECTRUM_1          = 0x02U,
    NI_FORMAT_SPECTRUM_2          = 0x03U,
    NI_FORMAT_TELEMETRY           = 0x04U,
    NI_FORMAT_SYNC_ORBIT_ATTITUDE = 0x05U,
    NI_FORMAT_GEOMAGNETIC         = 0x06U,
    NI_FORMAT_MCILWAIN            = 0x07U
} NiFormatType;

typedef struct {
    uint8_t observe_mode_number;
    uint32_t format_number;
    uint32_t rtc_seconds;
} NiFormatStamp;

typedef struct {
    uint16_t t_trig;
    uint16_t t_pe_dead;
    uint16_t amp_d;
    uint16_t trig_stat;
} NiEventRecord;

typedef struct {
    uint16_t n_d;
    uint16_t n_ac1;
    uint16_t n_ac2;
    uint16_t n_trig;
    uint16_t t_s_dead;
} NiCounters;

typedef struct {
    uint16_t mc_temp;
    uint16_t pu_temp;
    uint16_t ped_temp;
    uint16_t bd_temp;
    uint16_t pu_voltage;
    uint16_t pu_current;
    uint16_t ped_voltage;
    uint16_t ped_current;
    uint8_t hardware_config;
    uint16_t observe_params;
    uint16_t trigger_config;
    uint16_t alarm_status;
    uint16_t alarm_mask;
    uint16_t board_status;
    uint16_t ped_status_low;
    uint16_t ped_status_high;
    uint16_t belt_lmin;
    uint16_t belt_lmax;
    uint16_t belt_bmin;
    uint16_t ac1_rate_max;
} NiTelemetry;

size_t ni_format_events_words(size_t event_count);
size_t ni_format_spectrum1_words(uint16_t nhist);
size_t ni_format_spectrum2_words(const uint8_t* histogram);

size_t ni_format_build_events(const NiFormatStamp* stamp,
                              const NiEventRecord* events, size_t event_count,
                              uint8_t* out, size_t capacity);
size_t ni_format_build_counters(const NiFormatStamp* stamp, const NiCounters* counters,
                                uint8_t* out, size_t capacity);
size_t ni_format_build_spectrum1(const NiFormatStamp* stamp,
                                 const uint8_t* histogram, uint16_t nhist,
                                 uint8_t* out, size_t capacity);
size_t ni_format_build_spectrum2(const NiFormatStamp* stamp, const uint8_t* histogram,
                                 uint8_t* out, size_t capacity);
size_t ni_format_build_telemetry(const NiFormatStamp* stamp, const NiTelemetry* telemetry,
                                 uint8_t* out, size_t capacity);
size_t ni_format_build_sync_orbit(const NiFormatStamp* stamp, const InstrumentTime* sync_time,
                                  const uint8_t* kt, size_t kt_length,
                                  uint8_t* out, size_t capacity);
size_t ni_format_build_geomagnetic(const NiFormatStamp* stamp,
                                   const uint8_t* kt, size_t kt_length,
                                   uint8_t* out, size_t capacity);
size_t ni_format_build_mcilwain(const NiFormatStamp* stamp,
                                const uint8_t* kt, size_t kt_length,
                                uint8_t* out, size_t capacity);

#endif /* NATALIA_CORE_NI_FORMAT_H */
