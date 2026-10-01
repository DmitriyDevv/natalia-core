#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ni_format.h"
#include "ni_stream.h"
#include "observe_science.h"

#define CORE_BYTES      (NI_PACKET_CORE_WORDS * 2U)
#define CORE_OFFSET     (NI_PACKET_HEADER_WORDS * 2U)
#define MAX_PACKETS     64U
#define MAX_FORMATS     256U

#define PARAMS_EVENTS_1_NMAX_10_SP1_2048 0x0451U
#define PARAMS_EVENTS_1_NMAX_1           0x0009U
#define PARAMS_SP1_256                   0x0140U
#define PARAMS_SP2                       0x0080U
#define PARAMS_NONE                      0x0000U

typedef struct {
    uint8_t type;
    uint8_t mode;
    uint32_t number;
    uint32_t rtc;
    size_t words;
    size_t offset;
} ParsedFormat;

static ObserveScience science;
static NiStream stream;
static uint8_t collected[MAX_PACKETS * CORE_BYTES];
static size_t collected_packets;
static ParsedFormat formats[MAX_FORMATS];
static size_t format_count;
static NiTelemetry telemetry;

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

static ObserveScienceParams decode(uint16_t raw) {
    ObserveScienceParams params;

    memset(&params, 0, sizeof(params));
    assert(observe_science_decode_params(raw, &params));

    return params;
}

static void start_session(uint16_t raw_params, const ObserveScienceLimits* limits) {
    static const ObserveScienceLimits default_limits = {300, 150, 250, 100U};
    ObserveScienceParams params = decode(raw_params);

    ni_stream_begin(&stream, 1U, 0U, 0U);
    observe_science_begin(&science, &params, (limits != NULL) ? limits : &default_limits);
    collected_packets = 0U;
    format_count = 0U;
    memset(&telemetry, 0, sizeof(telemetry));
}

static void drain(void) {
    const uint8_t* packet;

    while ((packet = ni_stream_peek(&stream)) != NULL) {
        assert(collected_packets < MAX_PACKETS);
        memcpy(&collected[collected_packets * CORE_BYTES], &packet[CORE_OFFSET], CORE_BYTES);
        ++collected_packets;
        ni_stream_pop(&stream);
    }
}

static void finish_and_parse(void) {
    size_t total_words;
    size_t index = 0U;

    ni_stream_finish(&stream);
    drain();

    total_words = collected_packets * NI_PACKET_CORE_WORDS;
    format_count = 0U;

    while ((index + NI_FORMAT_HEADER_WORDS) <= total_words) {
        const uint8_t* format = &collected[index * 2U];
        ParsedFormat* parsed;

        if (word_at(format, 0U) == 0xAAAAU) {
            break;
        }

        assert(word_at(format, 0U) == 0xFEFAU);
        assert(format_count < MAX_FORMATS);

        parsed = &formats[format_count];
        parsed->type = (uint8_t)(word_at(format, 1U) & 0x00FFU);
        parsed->mode = (uint8_t)(word_at(format, 2U) & 0x00FFU);
        parsed->number = (uint32_t)word_at(format, 3U) | ((uint32_t)word_at(format, 4U) << 16);
        parsed->rtc = (uint32_t)word_at(format, 5U) | ((uint32_t)word_at(format, 6U) << 16);
        parsed->words = word_at(format, 7U);
        parsed->offset = index;

        assert((word_at(format, 1U) >> 8) == 0x0FU);
        assert((word_at(format, 2U) >> 8) == parsed->type);
        assert(word_at(format, 8U) == reference_crc(format, 16U));
        assert((index + parsed->words) <= total_words);
        assert(word_at(format, parsed->words - 1U) ==
               reference_crc(&format[18], (parsed->words - 10U) * 2U));
        assert(parsed->number == format_count);

        ++format_count;
        index += parsed->words;
    }
}

static uint16_t format_word(const ParsedFormat* format, size_t word) {
    return word_at(collected, format->offset + word);
}

static void second(uint32_t rtc, uint16_t n_ac1) {
    ObserveSecondMark mark;

    memset(&mark, 0, sizeof(mark));
    mark.rtc_seconds = rtc;
    mark.counters.n_d = (uint16_t)(rtc & 0xFFFFU);
    mark.counters.n_ac1 = n_ac1;
    mark.counters.n_ac2 = 3U;
    mark.counters.n_trig = 4U;
    mark.counters.t_s_dead = 5U;

    assert(observe_science_on_second(&science, &stream, &mark, &telemetry) == OBSERVE_SCIENCE_OK);
    drain();
}

static void event(uint16_t amplitude) {
    NiEventRecord record;

    record.t_trig = 0x1111U;
    record.t_pe_dead = 0x2222U;
    record.amp_d = amplitude;
    record.trig_stat = 0x4444U;

    assert(observe_science_on_event(&science, &stream, &record) == OBSERVE_SCIENCE_OK);
    drain();
}

static void put_float(uint8_t* bytes, float value) {
    uint32_t raw;

    memcpy(&raw, &value, sizeof(raw));
    bytes[0] = (uint8_t)(raw & 0xFFU);
    bytes[1] = (uint8_t)((raw >> 8) & 0xFFU);
    bytes[2] = (uint8_t)((raw >> 16) & 0xFFU);
    bytes[3] = (uint8_t)((raw >> 24) & 0xFFU);
}

static void geomagnetic(float measured_z_tesla, float calculated_z_tesla) {
    uint8_t kt[NI_KT_GEOMAGNETIC_BYTES];

    memset(kt, 0, sizeof(kt));
    put_float(&kt[8], measured_z_tesla);
    put_float(&kt[20], calculated_z_tesla);

    assert(observe_science_on_geomagnetic(&science, &stream, 0U, kt, sizeof(kt)) ==
           OBSERVE_SCIENCE_OK);
    drain();
}

static void mcilwain(int16_t l_x100, int16_t b_gs1000) {
    uint8_t kt[NI_KT_MCILWAIN_BYTES];

    memset(kt, 0, sizeof(kt));
    kt[20] = (uint8_t)((uint16_t)l_x100 & 0xFFU);
    kt[21] = (uint8_t)(((uint16_t)l_x100 >> 8) & 0xFFU);
    kt[22] = (uint8_t)((uint16_t)b_gs1000 & 0xFFU);
    kt[23] = (uint8_t)(((uint16_t)b_gs1000 >> 8) & 0xFFU);

    assert(observe_science_on_mcilwain(&science, &stream, 0U, kt, sizeof(kt)) ==
           OBSERVE_SCIENCE_OK);
    drain();
}

static size_t count_type(uint8_t type) {
    size_t count = 0U;
    size_t i;

    for (i = 0U; i < format_count; ++i) {
        if (formats[i].type == type) {
            ++count;
        }
    }

    return count;
}

static void decodes_observe_params(void) {
    ObserveScienceParams params = decode(PARAMS_EVENTS_1_NMAX_10_SP1_2048);

    assert(params.events_mode == 1U);
    assert(params.events_nmax == 10U);
    assert(params.spectrum_mode == 1U);
    assert(params.spectrum_bins == 2048U);

    params = decode(PARAMS_SP2);
    assert(params.spectrum_mode == 2U);
    assert(params.spectrum_bins == 2048U);
    assert(params.events_nmax == 0U);

    params = decode(0xF000U | PARAMS_SP1_256);
    assert(params.spectrum_bins == 256U);

    assert(!observe_science_decode_params(0x0800U, &params));
    assert(!observe_science_decode_params(0x0040U, &params));
    assert(!observe_science_decode_params(0x0180U, &params));
    assert(!observe_science_decode_params(0x0001U, &params));
    assert(!observe_science_decode_params(0x0008U, &params));
    assert(!observe_science_decode_params(0x0031U, &params));
    assert(!observe_science_decode_params(0x0007U | 0x0008U, &params));
}

static void first_second_writes_only_telemetry(void) {
    start_session(PARAMS_EVENTS_1_NMAX_10_SP1_2048, NULL);

    assert(observe_science_second_needs_telemetry(&science));
    event(100U);
    second(1000U, 0U);
    finish_and_parse();

    assert(format_count == 1U);
    assert(formats[0].type == 0x04U);
    assert(formats[0].rtc == 1000U);
    assert(formats[0].mode == 0U);
    assert(formats[0].words == 30U);
}

static void second_boundary_order(void) {
    start_session(PARAMS_EVENTS_1_NMAX_10_SP1_2048, NULL);

    second(1000U, 0U);
    event(0x0020U);
    event(0x0020U);
    event(0xFFFFU);
    second(1001U, 0U);
    finish_and_parse();

    assert(format_count == 4U);
    assert(formats[1].type == 0x00U);
    assert(formats[1].words == 22U);
    assert(formats[1].rtc == 1001U);
    assert(format_word(&formats[1], 9U) == 0x1111U);
    assert(format_word(&formats[1], 11U) == 0x0020U);
    assert(format_word(&formats[1], 12U) == 0x4444U);
    assert(formats[2].type == 0x01U);
    assert(format_word(&formats[2], 9U) == 1001U);
    assert(formats[3].type == 0x02U);
    assert(formats[3].words == 1035U);
    assert(format_word(&formats[3], 10U) == 0x0200U);
    assert(format_word(&formats[3], 10U + 1023U) == 0x0100U);
}

static void nmax_closes_events_mid_second(void) {
    start_session(PARAMS_EVENTS_1_NMAX_1, NULL);

    second(500U, 0U);
    event(1U);
    event(2U);
    second(501U, 0U);
    finish_and_parse();

    assert(format_count == 4U);
    assert(formats[1].type == 0x00U);
    assert(formats[1].words == 14U);
    assert(formats[1].rtc == 500U);
    assert(formats[2].type == 0x00U);
    assert(format_word(&formats[2], 11U) == 2U);
    assert(formats[3].type == 0x01U);
}

static void histogram_bins_and_saturation(void) {
    size_t i;

    start_session(PARAMS_SP1_256, NULL);
    second(10U, 0U);
    event(0x0000U);
    event(0x00FFU);
    event(0x0100U);
    for (i = 0U; i < 300U; ++i) {
        event(0xFFFFU);
    }
    second(11U, 0U);
    second(12U, 0U);
    finish_and_parse();

    assert(count_type(0x00U) == 0U);
    assert(formats[2].type == 0x02U);
    assert(formats[2].words == 139U);
    assert(format_word(&formats[2], 9U) == 0x0000U);
    assert(format_word(&formats[2], 10U) == 0x0102U);
    assert(format_word(&formats[2], 10U + 127U) == 0xFF00U);
    assert(formats[4].type == 0x02U);
    assert(format_word(&formats[4], 10U) == 0x0000U);
    assert(format_word(&formats[4], 10U + 127U) == 0x0000U);
}

static void spectrum2_uses_2048_bins(void) {
    start_session(PARAMS_SP2, NULL);
    second(20U, 0U);
    event((uint16_t)(5U << 5));
    event((uint16_t)(300U << 5));
    event((uint16_t)(300U << 5));
    second(21U, 0U);
    second(22U, 0U);
    finish_and_parse();

    assert(formats[2].type == 0x03U);
    assert(formats[2].words == 20U);
    assert(format_word(&formats[2], 9U) == 1U);
    assert(format_word(&formats[2], 10U) == 1U);
    assert(format_word(&formats[2], 17U) == 0x0501U);
    assert(format_word(&formats[2], 18U) == 0x2C02U);
    assert(formats[4].type == 0x03U);
    assert(formats[4].words == 18U);
}

static void telemetry_every_20_seconds(void) {
    ObserveSecondMark mark;
    uint32_t rtc;

    start_session(PARAMS_NONE, NULL);
    second(100U, 0U);

    for (rtc = 101U; rtc < 120U; ++rtc) {
        assert(!observe_science_second_needs_telemetry(&science));
        second(rtc, 0U);
    }

    assert(observe_science_second_needs_telemetry(&science));
    memset(&mark, 0, sizeof(mark));
    mark.rtc_seconds = 120U;
    assert(observe_science_on_second(&science, &stream, &mark, NULL) == OBSERVE_SCIENCE_INVALID_ARG);
    second(120U, 0U);
    finish_and_parse();

    assert(count_type(0x04U) == 2U);
    assert(count_type(0x01U) == 20U);
    assert(formats[format_count - 1U].type == 0x04U);
    assert(formats[format_count - 1U].rtc == 120U);
}

static void new_params_apply_at_next_second(void) {
    ObserveScienceParams params = decode(PARAMS_SP2);

    start_session(PARAMS_EVENTS_1_NMAX_10_SP1_2048, NULL);
    second(200U, 0U);
    event(7U);
    observe_science_request_params(&science, &params);
    assert(observe_science_second_needs_telemetry(&science));
    event(8U);
    second(201U, 0U);
    event(9U);
    second(202U, 0U);
    finish_and_parse();

    assert(formats[1].type == 0x00U);
    assert(formats[1].words == 18U);
    assert(formats[1].mode == 0U);
    assert(formats[2].type == 0x01U);
    assert(formats[2].mode == 0U);
    assert(formats[3].type == 0x02U);
    assert(formats[3].mode == 0U);
    assert(formats[4].type == 0x04U);
    assert(formats[4].mode == 1U);
    assert(formats[5].type == 0x01U);
    assert(formats[5].mode == 1U);
    assert(formats[6].type == 0x03U);
    assert(formats[6].words == 19U);
    assert(format_count == 7U);
    assert(observe_science_mode_number(&science) == 1U);
}

static void events_condition_mcilwain(void) {
    start_session(0x0014U, NULL);
    second(300U, 0U);

    event(1U);
    mcilwain(200, 500);
    event(2U);
    mcilwain(200, 250);
    event(3U);
    second(301U, 0U);
    finish_and_parse();

    assert(formats[1].type == 0x07U);
    assert(formats[2].type == 0x07U);
    assert(formats[3].type == 0x00U);
    assert(formats[3].words == 14U);
    assert(format_word(&formats[3], 11U) == 2U);
}

static void events_condition_outside_both_belts(void) {
    start_session(0x0015U, NULL);
    second(310U, 0U);

    mcilwain(200, 500);
    event(1U);
    mcilwain(100, 500);
    event(2U);
    mcilwain(300, 500);
    event(3U);
    second(311U, 0U);
    finish_and_parse();

    assert(count_type(0x00U) == 1U);
    assert(formats[4].type == 0x00U);
    assert(formats[4].words == 18U);
    assert(format_word(&formats[4], 11U) == 2U);
    assert(format_word(&formats[4], 15U) == 3U);
}

static void events_condition_geomagnetic(void) {
    start_session(0x0013U, NULL);
    second(320U, 0U);

    event(1U);
    geomagnetic(2.0e-5f, 5.0e-5f);
    event(2U);
    geomagnetic(5.0e-5f, 2.0e-5f);
    event(3U);
    second(321U, 0U);
    finish_and_parse();

    assert(count_type(0x06U) == 2U);
    assert(count_type(0x00U) == 1U);
    assert(formats[3].type == 0x00U);
    assert(formats[3].words == 14U);
    assert(format_word(&formats[3], 11U) == 3U);

    start_session(0x0012U, NULL);
    second(330U, 0U);
    geomagnetic(2.0e-5f, 5.0e-5f);
    event(4U);
    second(331U, 0U);
    finish_and_parse();

    assert(count_type(0x00U) == 1U);
    assert(format_word(&formats[2], 11U) == 4U);
}

static void events_condition_ac1_rate(void) {
    start_session(0x0016U, NULL);
    second(400U, 0U);
    event(1U);
    second(401U, 50U);
    event(2U);
    second(402U, 150U);
    event(3U);
    second(403U, 0U);
    finish_and_parse();

    assert(count_type(0x00U) == 1U);
    assert(formats[2].type == 0x00U);
    assert(format_word(&formats[2], 11U) == 2U);
}

static void kt_before_first_second_updates_state_only(void) {
    start_session(0x0014U, NULL);
    mcilwain(200, 500);
    second(500U, 0U);
    event(1U);
    second(501U, 0U);
    finish_and_parse();

    assert(count_type(0x07U) == 0U);
    assert(formats[0].type == 0x04U);
    assert(formats[1].type == 0x00U);
}

static void sync_orbit_is_written(void) {
    uint8_t kt[NI_KT_SYNC_ORBIT_BYTES];
    InstrumentTime received = {777U, 5U};

    memset(kt, 0x5A, sizeof(kt));
    start_session(PARAMS_NONE, NULL);
    assert(observe_science_on_sync_orbit(&science, &stream, &received, kt, sizeof(kt)) ==
           OBSERVE_SCIENCE_OK);
    second(776U, 0U);
    assert(observe_science_on_sync_orbit(&science, &stream, &received, kt, 10U) ==
           OBSERVE_SCIENCE_INVALID_ARG);
    assert(observe_science_on_sync_orbit(&science, &stream, &received, kt, sizeof(kt)) ==
           OBSERVE_SCIENCE_OK);
    finish_and_parse();

    assert(format_count == 2U);
    assert(formats[1].type == 0x05U);
    assert(formats[1].words == 76U);
    assert(formats[1].rtc == 777U);
    assert(format_word(&formats[1], 11U) == 5U);
}

static void no_room_leaves_state_unchanged(void) {
    static uint8_t filler[NI_STREAM_FIFO_PACKETS * NI_PACKET_CORE_WORDS * 2U];
    ObserveSecondMark mark;
    NiEventRecord record = {1U, 2U, 3U, 4U};
    uint32_t formats_before;
    size_t free_words;

    start_session(PARAMS_EVENTS_1_NMAX_1, NULL);
    second(600U, 0U);

    memset(filler, 0x5A, sizeof(filler));
    free_words = ni_stream_free_words(&stream);
    assert(ni_stream_append(&stream, filler, free_words - 13U));

    formats_before = observe_science_format_number(&science);
    assert(observe_science_on_event(&science, &stream, &record) == OBSERVE_SCIENCE_NO_ROOM);
    assert(science.event_count == 0U);

    memset(&mark, 0, sizeof(mark));
    mark.rtc_seconds = 601U;
    assert(observe_science_on_second(&science, &stream, &mark, &telemetry) ==
           OBSERVE_SCIENCE_NO_ROOM);
    assert(observe_science_format_number(&science) == formats_before);
    assert(science.current_rtc_seconds == 600U);

    ni_stream_pop(&stream);
    assert(observe_science_on_event(&science, &stream, &record) == OBSERVE_SCIENCE_OK);
    assert(observe_science_format_number(&science) == (formats_before + 1U));
    assert(observe_science_on_second(&science, &stream, &mark, &telemetry) == OBSERVE_SCIENCE_OK);
}

static void final_second_and_flush(void) {
    start_session(PARAMS_EVENTS_1_NMAX_10_SP1_2048, NULL);
    second(700U, 0U);
    event(1U);
    assert(observe_science_flush_events(&science, &stream) == OBSERVE_SCIENCE_OK);
    event(2U);
    observe_science_request_final(&science);
    assert(observe_science_second_needs_telemetry(&science));
    second(701U, 0U);
    finish_and_parse();

    assert(format_count == 6U);
    assert(formats[1].type == 0x00U);
    assert(formats[1].rtc == 700U);
    assert(formats[2].type == 0x00U);
    assert(formats[2].rtc == 701U);
    assert(formats[3].type == 0x01U);
    assert(formats[4].type == 0x02U);
    assert(formats[5].type == 0x04U);
}

int main(void) {
    decodes_observe_params();
    first_second_writes_only_telemetry();
    second_boundary_order();
    nmax_closes_events_mid_second();
    histogram_bins_and_saturation();
    spectrum2_uses_2048_bins();
    telemetry_every_20_seconds();
    new_params_apply_at_next_second();
    events_condition_mcilwain();
    events_condition_outside_both_belts();
    events_condition_geomagnetic();
    events_condition_ac1_rate();
    kt_before_first_second_updates_state_only();
    sync_orbit_is_written();
    no_room_leaves_state_unchanged();
    final_second_and_flush();

    return 0;
}
