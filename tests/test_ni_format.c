#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crc16.h"
#include "ni_format.h"

static uint8_t out[NI_FORMAT_MAX_BYTES];

static uint16_t reference_crc(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFFU;
    size_t i;
    int bit;

    for (i = 0U; i < size; ++i) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[i] << 8));
        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static uint16_t word_at(const uint8_t* data, size_t index) {
    return (uint16_t)((uint16_t)data[index * 2U] | ((uint16_t)data[(index * 2U) + 1U] << 8));
}

static NiFormatStamp make_stamp(uint8_t mode, uint32_t number, uint32_t rtc) {
    NiFormatStamp stamp;

    stamp.observe_mode_number = mode;
    stamp.format_number = number;
    stamp.rtc_seconds = rtc;

    return stamp;
}

static void check_frame(const uint8_t* data, size_t words, uint8_t type,
                        const NiFormatStamp* stamp) {
    assert(word_at(data, 0U) == 0xFEFAU);
    assert(word_at(data, 1U) == (uint16_t)(0x0F00U | type));
    assert(word_at(data, 2U) == (uint16_t)(((uint16_t)type << 8) | stamp->observe_mode_number));
    assert(word_at(data, 3U) == (uint16_t)(stamp->format_number & 0xFFFFU));
    assert(word_at(data, 4U) == (uint16_t)(stamp->format_number >> 16));
    assert(word_at(data, 5U) == (uint16_t)(stamp->rtc_seconds & 0xFFFFU));
    assert(word_at(data, 6U) == (uint16_t)(stamp->rtc_seconds >> 16));
    assert(word_at(data, 7U) == (uint16_t)words);
    assert(word_at(data, 8U) == reference_crc(data, 16U));
    assert(word_at(data, words - 1U) == reference_crc(&data[18], (words - 10U) * 2U));
}

static void crc_matches_known_vector(void) {
    static const uint8_t vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    assert(crc16_ccitt(vector, sizeof(vector)) == 0x29B1U);
    assert(crc16_ccitt_software(vector, sizeof(vector)) == 0x29B1U);
    assert(reference_crc(vector, sizeof(vector)) == 0x29B1U);
    assert(crc16_ccitt(vector, 0U) == 0xFFFFU);
}

static void counters_layout(void) {
    NiFormatStamp stamp = make_stamp(3U, 0x00012345UL, 0xABCD1234UL);
    NiCounters counters = {101U, 202U, 303U, 404U, 505U};
    size_t words = ni_format_build_counters(&stamp, &counters, out, sizeof(out));

    assert(words == 15U);
    check_frame(out, words, 0x01U, &stamp);
    assert(word_at(out, 9U) == 101U);
    assert(word_at(out, 10U) == 202U);
    assert(word_at(out, 11U) == 303U);
    assert(word_at(out, 12U) == 404U);
    assert(word_at(out, 13U) == 505U);
}

static void events_layout_and_limits(void) {
    static NiEventRecord events[NI_FORMAT_EVENTS_MAX + 1U];
    NiFormatStamp stamp = make_stamp(0U, 7U, 99U);
    size_t words;
    size_t i;

    for (i = 0U; i < (NI_FORMAT_EVENTS_MAX + 1U); ++i) {
        events[i].t_trig = (uint16_t)(0x1000U + i);
        events[i].t_pe_dead = (uint16_t)(0x2000U + i);
        events[i].amp_d = (uint16_t)(0x3000U + i);
        events[i].trig_stat = (uint16_t)(0x4000U + i);
    }

    words = ni_format_build_events(&stamp, events, 1U, out, sizeof(out));
    assert(words == 14U);
    check_frame(out, words, 0x00U, &stamp);
    assert(word_at(out, 9U) == 0x1000U);
    assert(word_at(out, 10U) == 0x2000U);
    assert(word_at(out, 11U) == 0x3000U);
    assert(word_at(out, 12U) == 0x4000U);

    words = ni_format_build_events(&stamp, events, 100U, out, sizeof(out));
    assert(words == 410U);
    check_frame(out, words, 0x00U, &stamp);
    assert(word_at(out, 9U + (99U * 4U)) == (uint16_t)(0x1000U + 99U));
    assert(word_at(out, 12U + (99U * 4U)) == (uint16_t)(0x4000U + 99U));

    assert(ni_format_build_events(&stamp, events, 0U, out, sizeof(out)) == 0U);
    assert(ni_format_build_events(&stamp, events, 101U, out, sizeof(out)) == 0U);
    assert(ni_format_build_events(&stamp, events, 1U, out, 27U) == 0U);
}

static void spectrum1_sizes_and_packing(void) {
    static uint8_t histogram[NI_FORMAT_SPECTRUM_BINS_MAX];
    static const uint16_t bins[] = {256U, 512U, 1024U, 2048U};
    static const size_t expected_words[] = {139U, 267U, 523U, 1035U};
    NiFormatStamp stamp = make_stamp(1U, 2U, 3U);
    size_t words;
    size_t i;

    memset(histogram, 0, sizeof(histogram));
    histogram[0] = 0x11U;
    histogram[1] = 0x22U;
    histogram[255] = 0xFFU;

    for (i = 0U; i < 4U; ++i) {
        words = ni_format_build_spectrum1(&stamp, histogram, bins[i], out, sizeof(out));
        assert(words == expected_words[i]);
        assert(ni_format_spectrum1_words(bins[i]) == expected_words[i]);
        check_frame(out, words, 0x02U, &stamp);
        assert(word_at(out, 9U) == (uint16_t)((i << 8) | i));
        assert(word_at(out, 10U) == 0x2211U);
        assert(word_at(out, 10U + 127U) == 0xFF00U);
    }

    assert(ni_format_build_spectrum1(&stamp, histogram, 300U, out, sizeof(out)) == 0U);
    assert(ni_format_spectrum1_words(0U) == 0U);
}

static void spectrum2_zero_suppression(void) {
    static uint8_t histogram[NI_FORMAT_SPECTRUM_BINS_MAX];
    NiFormatStamp stamp = make_stamp(0U, 4U, 5U);
    size_t words;
    size_t i;

    memset(histogram, 0, sizeof(histogram));
    words = ni_format_build_spectrum2(&stamp, histogram, out, sizeof(out));
    assert(words == 18U);
    check_frame(out, words, 0x03U, &stamp);
    for (i = 9U; i < 17U; ++i) {
        assert(word_at(out, i) == 0U);
    }

    histogram[5] = 3U;
    histogram[300] = 1U;
    histogram[301] = 7U;
    words = ni_format_build_spectrum2(&stamp, histogram, out, sizeof(out));
    assert(words == 21U);
    assert(ni_format_spectrum2_words(histogram) == 21U);
    check_frame(out, words, 0x03U, &stamp);
    assert(word_at(out, 9U) == 1U);
    assert(word_at(out, 10U) == 2U);
    for (i = 11U; i < 17U; ++i) {
        assert(word_at(out, i) == 0U);
    }
    assert(word_at(out, 17U) == 0x0503U);
    assert(word_at(out, 18U) == 0x2C01U);
    assert(word_at(out, 19U) == 0x2D07U);

    memset(histogram, 1, sizeof(histogram));
    words = ni_format_build_spectrum2(&stamp, histogram, out, sizeof(out));
    assert(words == 2066U);
    check_frame(out, words, 0x03U, &stamp);
    for (i = 9U; i < 17U; ++i) {
        assert(word_at(out, i) == 256U);
    }
    assert(word_at(out, 17U + 2047U) == 0xFF01U);
}

static void telemetry_layout(void) {
    NiFormatStamp stamp = make_stamp(9U, 10U, 11U);
    NiTelemetry telemetry;
    size_t words;

    telemetry.mc_temp = 1U;
    telemetry.pu_temp = 2U;
    telemetry.ped_temp = 3U;
    telemetry.bd_temp = 4U;
    telemetry.pu_voltage = 5U;
    telemetry.pu_current = 6U;
    telemetry.ped_voltage = 7U;
    telemetry.ped_current = 8U;
    telemetry.hardware_config = 0xA5U;
    telemetry.observe_params = 10U;
    telemetry.trigger_config = 11U;
    telemetry.alarm_status = 12U;
    telemetry.alarm_mask = 13U;
    telemetry.board_status = 14U;
    telemetry.ped_status_low = 15U;
    telemetry.ped_status_high = 16U;
    telemetry.belt_lmin = 17U;
    telemetry.belt_lmax = 18U;
    telemetry.belt_bmin = 19U;
    telemetry.ac1_rate_max = 20U;

    words = ni_format_build_telemetry(&stamp, &telemetry, out, sizeof(out));
    assert(words == 30U);
    check_frame(out, words, 0x04U, &stamp);
    assert(word_at(out, 9U) == 1U);
    assert(word_at(out, 16U) == 8U);
    assert(word_at(out, 17U) == 0x00A5U);
    assert(word_at(out, 18U) == 10U);
    assert(word_at(out, 23U) == 15U);
    assert(word_at(out, 24U) == 16U);
    assert(word_at(out, 28U) == 20U);
}

static void sync_orbit_layout(void) {
    uint8_t kt[NI_KT_SYNC_ORBIT_BYTES];
    InstrumentTime received = {0x00056789UL, 321U};
    NiFormatStamp stamp = make_stamp(2U, 12U, 0x00056789UL);
    size_t words;
    size_t i;

    for (i = 0U; i < sizeof(kt); ++i) {
        kt[i] = (uint8_t)i;
    }

    words = ni_format_build_sync_orbit(&stamp, &received, kt, sizeof(kt), out, sizeof(out));
    assert(words == 76U);
    check_frame(out, words, 0x05U, &stamp);
    assert(word_at(out, 9U) == 0x6789U);
    assert(word_at(out, 10U) == 0x0005U);
    assert(word_at(out, 11U) == 321U);
    assert(word_at(out, 12U) == 0x0100U);
    assert(word_at(out, 15U) == 0x0706U);
    assert(word_at(out, 16U) == 0x0908U);
    assert(word_at(out, 17U) == 0x0B0AU);
    assert(word_at(out, 18U) == 0x0D0CU);
    assert(word_at(out, 31U) == 0x2726U);
    assert(word_at(out, 32U) == 0x2A29U);
    assert(word_at(out, 45U) == 0x4443U);
    assert(word_at(out, 46U) == 0x4746U);
    assert(word_at(out, 59U) == 0x6160U);
    assert(word_at(out, 60U) == 0x6463U);
    assert(word_at(out, 65U) == 0x6E6DU);
    assert(word_at(out, 66U) == 0x4528U);
    assert(word_at(out, 67U) == 0x6F62U);
    assert(word_at(out, 68U) == 0x7170U);
    assert(word_at(out, 69U) == 0x7372U);
    assert(word_at(out, 72U) == 0x7978U);
    assert(word_at(out, 73U) == 0x7B7AU);
    assert(word_at(out, 74U) == 0x007CU);

    assert(ni_format_build_sync_orbit(&stamp, &received, kt, 124U, out, sizeof(out)) == 0U);
}

static void kt_copies_are_verbatim(void) {
    uint8_t kt[NI_KT_GEOMAGNETIC_BYTES];
    NiFormatStamp stamp = make_stamp(0U, 1U, 2U);
    size_t words;
    size_t i;

    for (i = 0U; i < sizeof(kt); ++i) {
        kt[i] = (uint8_t)(0x80U + i);
    }

    words = ni_format_build_geomagnetic(&stamp, kt, NI_KT_GEOMAGNETIC_BYTES, out, sizeof(out));
    assert(words == 48U);
    check_frame(out, words, 0x06U, &stamp);
    assert(memcmp(&out[18], kt, NI_KT_GEOMAGNETIC_BYTES) == 0);

    words = ni_format_build_mcilwain(&stamp, kt, NI_KT_MCILWAIN_BYTES, out, sizeof(out));
    assert(words == 22U);
    check_frame(out, words, 0x07U, &stamp);
    assert(memcmp(&out[18], kt, NI_KT_MCILWAIN_BYTES) == 0);

    assert(ni_format_build_geomagnetic(&stamp, kt, 75U, out, sizeof(out)) == 0U);
    assert(ni_format_build_mcilwain(&stamp, kt, 25U, out, sizeof(out)) == 0U);
}

static void rejects_small_capacity(void) {
    NiFormatStamp stamp = make_stamp(0U, 0U, 0U);
    NiCounters counters = {0U, 0U, 0U, 0U, 0U};

    assert(ni_format_build_counters(&stamp, &counters, out, 29U) == 0U);
    assert(ni_format_build_counters(&stamp, &counters, out, 30U) == 15U);
    assert(ni_format_build_counters(NULL, &counters, out, sizeof(out)) == 0U);
    assert(ni_format_build_counters(&stamp, &counters, NULL, sizeof(out)) == 0U);
}

int main(void) {
    crc_matches_known_vector();
    counters_layout();
    events_layout_and_limits();
    spectrum1_sizes_and_packing();
    spectrum2_zero_suppression();
    telemetry_layout();
    sync_orbit_layout();
    kt_copies_are_verbatim();
    rejects_small_capacity();

    return 0;
}
