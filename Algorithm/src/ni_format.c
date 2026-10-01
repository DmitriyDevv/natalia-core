#include "ni_format.h"

#include <stdbool.h>
#include <string.h>

#include "crc16.h"

#define NI_FORMAT_BYTES(words) ((words) * 2U)

static void ni_format_put_word(uint8_t* out, size_t index, uint16_t value) {
    out[NI_FORMAT_BYTES(index)] = (uint8_t)(value & 0x00FFU);
    out[NI_FORMAT_BYTES(index) + 1U] = (uint8_t)((value >> 8) & 0x00FFU);
}

static uint16_t ni_format_pack_bytes(uint8_t high, uint8_t low) {
    return (uint16_t)(((uint16_t)high << 8) | (uint16_t)low);
}

static void ni_format_copy_words(uint8_t* out, size_t index, const uint8_t* source, size_t words) {
    (void)memcpy(&out[NI_FORMAT_BYTES(index)], source, NI_FORMAT_BYTES(words));
}

static bool ni_format_can_build(const NiFormatStamp* stamp, const uint8_t* out,
                                size_t capacity, size_t words) {
    return (stamp != NULL) && (out != NULL) && (NI_FORMAT_BYTES(words) <= capacity);
}

static void ni_format_put_header(uint8_t* out, NiFormatType type,
                                 const NiFormatStamp* stamp, size_t total_words) {
    uint8_t type_code = (uint8_t)type;

    ni_format_put_word(out, 0U, NI_FORMAT_MARK);
    ni_format_put_word(out, 1U, ni_format_pack_bytes(NI_FORMAT_CLASS_CODE, type_code));
    ni_format_put_word(out, 2U, ni_format_pack_bytes(type_code, stamp->observe_mode_number));
    ni_format_put_word(out, 3U, (uint16_t)(stamp->format_number & 0xFFFFU));
    ni_format_put_word(out, 4U, (uint16_t)((stamp->format_number >> 16) & 0xFFFFU));
    ni_format_put_word(out, 5U, (uint16_t)(stamp->rtc_seconds & 0xFFFFU));
    ni_format_put_word(out, 6U, (uint16_t)((stamp->rtc_seconds >> 16) & 0xFFFFU));
    ni_format_put_word(out, 7U, (uint16_t)total_words);
    ni_format_put_word(out, 8U, crc16_ccitt(out, NI_FORMAT_BYTES(NI_FORMAT_HEADER_WORDS - 1U)));
}

static size_t ni_format_finish(uint8_t* out, size_t total_words) {
    size_t content_words = total_words - NI_FORMAT_HEADER_WORDS - 1U;
    uint16_t crc = crc16_ccitt(&out[NI_FORMAT_BYTES(NI_FORMAT_HEADER_WORDS)],
                               NI_FORMAT_BYTES(content_words));

    ni_format_put_word(out, total_words - 1U, crc);

    return total_words;
}

static bool ni_format_phist_from_nhist(uint16_t nhist, uint8_t* phist) {
    switch (nhist) {
    case 256U:
        *phist = 0U;
        return true;
    case 512U:
        *phist = 1U;
        return true;
    case 1024U:
        *phist = 2U;
        return true;
    case 2048U:
        *phist = 3U;
        return true;
    default:
        return false;
    }
}

size_t ni_format_events_words(size_t event_count) {
    if ((event_count == 0U) || (event_count > NI_FORMAT_EVENTS_MAX)) {
        return 0U;
    }

    return NI_FORMAT_HEADER_WORDS + (event_count * NI_FORMAT_EVENT_WORDS) + 1U;
}

size_t ni_format_spectrum1_words(uint16_t nhist) {
    uint8_t phist;

    if (!ni_format_phist_from_nhist(nhist, &phist)) {
        return 0U;
    }

    return NI_FORMAT_HEADER_WORDS + 1U + ((size_t)nhist / 2U) + 1U;
}

size_t ni_format_spectrum2_words(const uint8_t* histogram) {
    size_t nonzero = 0U;
    size_t bin;

    if (histogram == NULL) {
        return 0U;
    }

    for (bin = 0U; bin < NI_FORMAT_SPECTRUM_BINS_MAX; ++bin) {
        if (histogram[bin] != 0U) {
            ++nonzero;
        }
    }

    return NI_FORMAT_SPECTRUM2_MIN_WORDS + nonzero;
}

size_t ni_format_build_events(const NiFormatStamp* stamp,
                              const NiEventRecord* events, size_t event_count,
                              uint8_t* out, size_t capacity) {
    size_t total_words = ni_format_events_words(event_count);
    size_t i;
    size_t index;

    if ((total_words == 0U) || (events == NULL) ||
        !ni_format_can_build(stamp, out, capacity, total_words)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_EVENTS, stamp, total_words);

    index = NI_FORMAT_HEADER_WORDS;
    for (i = 0U; i < event_count; ++i) {
        ni_format_put_word(out, index, events[i].t_trig);
        ni_format_put_word(out, index + 1U, events[i].t_pe_dead);
        ni_format_put_word(out, index + 2U, events[i].amp_d);
        ni_format_put_word(out, index + 3U, events[i].trig_stat);
        index += NI_FORMAT_EVENT_WORDS;
    }

    return ni_format_finish(out, total_words);
}

size_t ni_format_build_counters(const NiFormatStamp* stamp, const NiCounters* counters,
                                uint8_t* out, size_t capacity) {
    if ((counters == NULL) ||
        !ni_format_can_build(stamp, out, capacity, NI_FORMAT_COUNTERS_WORDS)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_COUNTERS, stamp, NI_FORMAT_COUNTERS_WORDS);
    ni_format_put_word(out, 9U, counters->n_d);
    ni_format_put_word(out, 10U, counters->n_ac1);
    ni_format_put_word(out, 11U, counters->n_ac2);
    ni_format_put_word(out, 12U, counters->n_trig);
    ni_format_put_word(out, 13U, counters->t_s_dead);

    return ni_format_finish(out, NI_FORMAT_COUNTERS_WORDS);
}

size_t ni_format_build_spectrum1(const NiFormatStamp* stamp,
                                 const uint8_t* histogram, uint16_t nhist,
                                 uint8_t* out, size_t capacity) {
    size_t total_words = ni_format_spectrum1_words(nhist);
    uint8_t phist = 0U;
    size_t pair;

    if ((total_words == 0U) || (histogram == NULL) ||
        !ni_format_can_build(stamp, out, capacity, total_words)) {
        return 0U;
    }

    (void)ni_format_phist_from_nhist(nhist, &phist);

    ni_format_put_header(out, NI_FORMAT_SPECTRUM_1, stamp, total_words);
    ni_format_put_word(out, NI_FORMAT_HEADER_WORDS, ni_format_pack_bytes(phist, phist));

    for (pair = 0U; pair < ((size_t)nhist / 2U); ++pair) {
        ni_format_put_word(out, NI_FORMAT_HEADER_WORDS + 1U + pair,
                           ni_format_pack_bytes(histogram[(pair * 2U) + 1U],
                                                histogram[pair * 2U]));
    }

    return ni_format_finish(out, total_words);
}

size_t ni_format_build_spectrum2(const NiFormatStamp* stamp, const uint8_t* histogram,
                                 uint8_t* out, size_t capacity) {
    size_t total_words = ni_format_spectrum2_words(histogram);
    size_t group;
    size_t bin;
    size_t index;
    uint16_t nonzero;

    if ((total_words == 0U) || !ni_format_can_build(stamp, out, capacity, total_words)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_SPECTRUM_2, stamp, total_words);

    index = NI_FORMAT_HEADER_WORDS + NI_FORMAT_SPECTRUM2_GROUPS;
    for (group = 0U; group < NI_FORMAT_SPECTRUM2_GROUPS; ++group) {
        const uint8_t* group_bins = &histogram[group * NI_FORMAT_SPECTRUM2_GROUP_BINS];

        nonzero = 0U;
        for (bin = 0U; bin < NI_FORMAT_SPECTRUM2_GROUP_BINS; ++bin) {
            if (group_bins[bin] != 0U) {
                ni_format_put_word(out, index, ni_format_pack_bytes((uint8_t)bin, group_bins[bin]));
                ++index;
                ++nonzero;
            }
        }

        ni_format_put_word(out, NI_FORMAT_HEADER_WORDS + group, nonzero);
    }

    return ni_format_finish(out, total_words);
}

size_t ni_format_build_telemetry(const NiFormatStamp* stamp, const NiTelemetry* telemetry,
                                 uint8_t* out, size_t capacity) {
    if ((telemetry == NULL) ||
        !ni_format_can_build(stamp, out, capacity, NI_FORMAT_TELEMETRY_WORDS)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_TELEMETRY, stamp, NI_FORMAT_TELEMETRY_WORDS);
    ni_format_put_word(out, 9U, telemetry->mc_temp);
    ni_format_put_word(out, 10U, telemetry->pu_temp);
    ni_format_put_word(out, 11U, telemetry->ped_temp);
    ni_format_put_word(out, 12U, telemetry->bd_temp);
    ni_format_put_word(out, 13U, telemetry->pu_voltage);
    ni_format_put_word(out, 14U, telemetry->pu_current);
    ni_format_put_word(out, 15U, telemetry->ped_voltage);
    ni_format_put_word(out, 16U, telemetry->ped_current);
    ni_format_put_word(out, 17U, ni_format_pack_bytes(0U, telemetry->hardware_config));
    ni_format_put_word(out, 18U, telemetry->observe_params);
    ni_format_put_word(out, 19U, telemetry->trigger_config);
    ni_format_put_word(out, 20U, telemetry->alarm_status);
    ni_format_put_word(out, 21U, telemetry->alarm_mask);
    ni_format_put_word(out, 22U, telemetry->board_status);
    ni_format_put_word(out, 23U, telemetry->ped_status_low);
    ni_format_put_word(out, 24U, telemetry->ped_status_high);
    ni_format_put_word(out, 25U, telemetry->belt_lmin);
    ni_format_put_word(out, 26U, telemetry->belt_lmax);
    ni_format_put_word(out, 27U, telemetry->belt_bmin);
    ni_format_put_word(out, 28U, telemetry->ac1_rate_max);

    return ni_format_finish(out, NI_FORMAT_TELEMETRY_WORDS);
}

size_t ni_format_build_sync_orbit(const NiFormatStamp* stamp, const InstrumentTime* sync_time,
                                  const uint8_t* kt, size_t kt_length,
                                  uint8_t* out, size_t capacity) {
    if ((sync_time == NULL) || (kt == NULL) || (kt_length != NI_KT_SYNC_ORBIT_BYTES) ||
        !ni_format_can_build(stamp, out, capacity, NI_FORMAT_SYNC_ORBIT_WORDS)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_SYNC_ORBIT_ATTITUDE, stamp, NI_FORMAT_SYNC_ORBIT_WORDS);
    ni_format_put_word(out, 9U, (uint16_t)(sync_time->seconds & 0xFFFFU));
    ni_format_put_word(out, 10U, (uint16_t)((sync_time->seconds >> 16) & 0xFFFFU));
    ni_format_put_word(out, 11U, sync_time->milliseconds);
    ni_format_copy_words(out, 12U, &kt[0], 4U);
    ni_format_copy_words(out, 16U, &kt[8], 2U);
    ni_format_copy_words(out, 18U, &kt[12], 14U);
    ni_format_copy_words(out, 32U, &kt[41], 14U);
    ni_format_copy_words(out, 46U, &kt[70], 14U);
    ni_format_copy_words(out, 60U, &kt[99], 6U);
    ni_format_put_word(out, 66U, ni_format_pack_bytes(kt[69], kt[40]));
    ni_format_put_word(out, 67U, ni_format_pack_bytes(kt[111], kt[98]));
    ni_format_put_word(out, 68U, ni_format_pack_bytes(kt[113], kt[112]));
    ni_format_copy_words(out, 69U, &kt[114], 4U);
    ni_format_put_word(out, 73U, ni_format_pack_bytes(kt[123], kt[122]));
    ni_format_put_word(out, 74U, ni_format_pack_bytes(0U, kt[124]));

    return ni_format_finish(out, NI_FORMAT_SYNC_ORBIT_WORDS);
}

size_t ni_format_build_geomagnetic(const NiFormatStamp* stamp,
                                   const uint8_t* kt, size_t kt_length,
                                   uint8_t* out, size_t capacity) {
    if ((kt == NULL) || (kt_length != NI_KT_GEOMAGNETIC_BYTES) ||
        !ni_format_can_build(stamp, out, capacity, NI_FORMAT_GEOMAGNETIC_WORDS)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_GEOMAGNETIC, stamp, NI_FORMAT_GEOMAGNETIC_WORDS);
    ni_format_copy_words(out, NI_FORMAT_HEADER_WORDS, kt, NI_KT_GEOMAGNETIC_BYTES / 2U);

    return ni_format_finish(out, NI_FORMAT_GEOMAGNETIC_WORDS);
}

size_t ni_format_build_mcilwain(const NiFormatStamp* stamp,
                                const uint8_t* kt, size_t kt_length,
                                uint8_t* out, size_t capacity) {
    if ((kt == NULL) || (kt_length != NI_KT_MCILWAIN_BYTES) ||
        !ni_format_can_build(stamp, out, capacity, NI_FORMAT_MCILWAIN_WORDS)) {
        return 0U;
    }

    ni_format_put_header(out, NI_FORMAT_MCILWAIN, stamp, NI_FORMAT_MCILWAIN_WORDS);
    ni_format_copy_words(out, NI_FORMAT_HEADER_WORDS, kt, NI_KT_MCILWAIN_BYTES / 2U);

    return ni_format_finish(out, NI_FORMAT_MCILWAIN_WORDS);
}
