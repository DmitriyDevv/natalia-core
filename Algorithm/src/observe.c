#include "observe.h"

#include <string.h>

#include "alarm.h"
#include "alarm_monitor.h"
#include "event_queue.h"
#include "mram_store.h"
#include "transport.h"

static uint8_t observe_bank_id(NandBank bank) {
    return (uint8_t)bank;
}

static void observe_report_end(SystemContext *ctx, EventType type) {
    if (!ctx->observe.end_reported) {
        ctx->observe.end_reported = true;
        (void)system_event_queue_push_back_type(type);
    }
}

static void observe_fail(SystemContext *ctx, uint32_t alarm_bit) {
    ctx->observe.operation_failed = true;
    observe_abort(ctx);
    ctx->observe.stage = OBSERVE_STAGE_EXIT_ALARM;
    alarm_raise(ctx, alarm_bit);
    observe_report_end(ctx, EVENT_OBSERVE_DONE);
}

static void observe_fail_writer(SystemContext *ctx) {
    observe_fail(ctx, ni_writer_mram_failed(&ctx->observe.writer) ? ALARM_MRAM : ALARM_NAND_PR);
}

uint8_t observe_hardware_config(const SystemContext *ctx) {
    const ObserveContext *observe = &ctx->observe;
    uint8_t config = (uint8_t)((uint8_t)observe->bank & 0x03U);

    if (observe->ped_power_enabled) {
        config |= (uint8_t)(1U << 2U);
    }
    if (observe->ped_sleep_enabled) {
        config |= (uint8_t)(1U << 3U);
    }
    if (observe->registration_enabled) {
        config |= (uint8_t)(1U << 4U);
    }
    if (observe->bank_power_after_full == POWER_AFTER_DONE_KEEP) {
        config |= (uint8_t)(1U << 5U);
    }
    if (observe->ped_power_after_full) {
        config |= (uint8_t)(1U << 6U);
    }
    if (observe->ped_sleep_after_full) {
        config |= (uint8_t)(1U << 7U);
    }

    return config;
}

static void observe_build_telemetry(SystemContext *ctx, NiTelemetry *telemetry) {
    const ObserveScienceLimits *limits = &ctx->observe.science.limits;
    TransportMeasurements measurements;
    uint32_t power_status = 0U;
    uint32_t ped_status = 0U;

    transport_read_measurements(&measurements);
    (void)board_read_power_status(&power_status);

    if (ctx->ped.is_powered && (board_ped_read_status(&ped_status) == BOARD_OK)) {
        ctx->ped.status = ped_status;
    }

    (void)memset(telemetry, 0, sizeof(*telemetry));
    telemetry->mc_temp = measurements.mc_temp;
    telemetry->pu_temp = measurements.pu_temp;
    telemetry->ped_temp = measurements.ped_temp;
    telemetry->bd_temp = measurements.bd_temp;
    telemetry->pu_voltage = measurements.pu_voltage;
    telemetry->pu_current = measurements.pu_current;
    telemetry->ped_voltage = measurements.ped_voltage;
    telemetry->ped_current = measurements.ped_current;
    telemetry->hardware_config = observe_hardware_config(ctx);
    telemetry->observe_params = ctx->observe.observe_params;
    telemetry->trigger_config = ctx->observe.trigger_config;
    telemetry->alarm_status = (uint16_t)(ctx->alarm_status & 0xFFFFU);
    telemetry->alarm_mask = (uint16_t)(ctx->alarm_mask & 0xFFFFU);
    telemetry->board_status = (uint16_t)(power_status & 0xFFFFU);
    telemetry->ped_status_low = (uint16_t)(ctx->ped.status & 0xFFFFU);
    telemetry->ped_status_high = (uint16_t)(ctx->ped.status >> 16U);
    telemetry->belt_lmin = (uint16_t)limits->belt_lmin;
    telemetry->belt_lmax = (uint16_t)limits->belt_lmax;
    telemetry->belt_bmin = (uint16_t)limits->belt_bmin;
    telemetry->ac1_rate_max = limits->ac1_rate_max;
}

static void observe_start_flush(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;

    (void)board_ped_acquisition_stop();
    ni_stream_finish(&observe->stream);
    ni_writer_request_finish(&observe->writer);
    observe->record_count = 0U;
    observe->record_index = 0U;
    observe->stage = OBSERVE_STAGE_FLUSHING;
}

static ObserveScienceStatus observe_process_second(SystemContext *ctx, const BoardPedRecord *record) {
    ObserveContext *observe = &ctx->observe;
    ObserveSecondMark mark;
    InstrumentTime now;
    bool final_second = observe->science.final_requested;
    ObserveScienceStatus status;

    mark.counters.n_d = record->data[0];
    mark.counters.n_ac1 = record->data[1];
    mark.counters.n_ac2 = record->data[2];
    mark.counters.n_trig = record->data[3];
    mark.counters.t_s_dead = record->data[4];
    mark.rtc_seconds = record->rtc_seconds;

    if (((record->flags & BOARD_PED_RECORD_FLAG_NO_TIME) != 0U) &&
        (board_rtc_get_time(&now) == BOARD_OK)) {
        mark.rtc_seconds = now.seconds;
    }

    if (!observe->telemetry_ready && observe_science_second_needs_telemetry(&observe->science)) {
        observe_build_telemetry(ctx, &observe->telemetry);
        observe->telemetry_ready = true;
    }

    status = observe_science_on_second(&observe->science, &observe->stream, &mark,
                                       &observe->telemetry);

    if ((status == OBSERVE_SCIENCE_OK) && final_second &&
        (observe->stage == OBSERVE_STAGE_FINISHING)) {
        observe_start_flush(ctx);
    }

    return status;
}

static ObserveScienceStatus observe_process_record(SystemContext *ctx, const BoardPedRecord *record) {
    ObserveContext *observe = &ctx->observe;
    NiEventRecord event;

    if (record->kind == BOARD_PED_RECORD_EVENT) {
        event.t_trig = record->data[0];
        event.t_pe_dead = record->data[1];
        event.amp_d = record->data[2];
        event.trig_stat = record->data[3];

        return observe_science_on_event(&observe->science, &observe->stream, &event);
    }

    if (record->kind == BOARD_PED_RECORD_SECOND) {
        return observe_process_second(ctx, record);
    }

    return OBSERVE_SCIENCE_OK;
}

static bool observe_pump_records(SystemContext *ctx, uint32_t budget) {
    ObserveContext *observe = &ctx->observe;
    size_t taken = 0U;
    const BoardPedRecord *record;
    ObserveScienceStatus status;

    while (budget > 0U) {
        if (observe->record_index >= observe->record_count) {
            observe->record_count = 0U;
            observe->record_index = 0U;

            if ((board_ped_take_records(observe->records, OBSERVE_RECORD_BATCH, &taken) != BOARD_OK) ||
                (taken == 0U)) {
                return false;
            }

            observe->record_count = (uint8_t)taken;
            observe->stats.records_taken += (uint32_t)taken;
        }

        record = &observe->records[observe->record_index];
        status = observe_process_record(ctx, record);
        if (status == OBSERVE_SCIENCE_NO_ROOM) {
            ++observe->stats.records_held;
            return true;
        }

        if (record->kind == BOARD_PED_RECORD_EVENT) {
            ++observe->stats.events_taken;
        } else if (record->kind == BOARD_PED_RECORD_SECOND) {
            ++observe->stats.seconds_taken;
        }

        observe->telemetry_ready = false;
        ++observe->record_index;
        --budget;

        if (observe->stage != OBSERVE_STAGE_ACTIVE && observe->stage != OBSERVE_STAGE_FINISHING) {
            return false;
        }
    }

    return true;
}

static ObserveScienceStatus observe_apply_kt(SystemContext *ctx, ObserveKtType type,
                                             const ObserveKtLatch *latch) {
    ObserveContext *observe = &ctx->observe;

    switch (type) {
    case OBSERVE_KT_SYNC_ORBIT:
        return observe_science_on_sync_orbit(&observe->science, &observe->stream,
                                             &latch->received_at, latch->data, latch->length);

    case OBSERVE_KT_GEOMAGNETIC:
        return observe_science_on_geomagnetic(&observe->science, &observe->stream,
                                              latch->received_at.seconds, latch->data, latch->length);

    case OBSERVE_KT_MCILWAIN:
        return observe_science_on_mcilwain(&observe->science, &observe->stream,
                                           latch->received_at.seconds, latch->data, latch->length);

    case OBSERVE_KT_COUNT:
    default:
        return OBSERVE_SCIENCE_INVALID_ARG;
    }
}

static void observe_pump_kt(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;
    uint32_t type;

    for (type = 0U; type < (uint32_t)OBSERVE_KT_COUNT; ++type) {
        ObserveKtLatch *latch = &observe->kt[type];

        if (!latch->pending) {
            continue;
        }

        if (observe_apply_kt(ctx, (ObserveKtType)type, latch) == OBSERVE_SCIENCE_NO_ROOM) {
            ++observe->stats.kt_deferred;
            continue;
        }

        latch->pending = false;
    }
}

static void observe_start_session(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;
    ObserveScienceParams params;
    ObserveScienceLimits limits;
    MramStoreConfig config;
    MramStoreServiceData service_data;

    (void)memset(&params, 0, sizeof(params));
    (void)memset(&limits, 0, sizeof(limits));
    (void)memset(&config, 0, sizeof(config));
    (void)memset(&service_data, 0, sizeof(service_data));

    ++ctx->observe_session_id;

    if (mram_store_load_service_data(&service_data) == BOARD_OK) {
        service_data.observe_session_id = ctx->observe_session_id;
        if (mram_store_save_service_data(&service_data) != BOARD_OK) {
            alarm_raise(ctx, ALARM_MRAM);
        }
    } else {
        alarm_raise(ctx, ALARM_MRAM);
    }

    if (mram_store_load_config(&config) == BOARD_OK) {
        limits.belt_bmin = config.belt_bmin;
        limits.belt_lmin = config.belt_lmin;
        limits.belt_lmax = config.belt_lmax;
        limits.ac1_rate_max = config.ac1_rate_max;
    } else {
        alarm_raise(ctx, ALARM_MRAM);
    }

    (void)observe_science_decode_params(observe->observe_params, &params);

    ni_stream_begin(&observe->stream, ctx->observe_session_id,
                    ni_writer_next_packet(&observe->writer),
                    ni_writer_last_crc(&observe->writer));
    observe_science_begin(&observe->science, &params, &limits);
    observe_science_set_events_wait_kt(&observe->science, observe->events_wait_kt);

    observe->record_count = 0U;
    observe->record_index = 0U;
    observe->telemetry_ready = false;

    if (board_ped_acquisition_start() != BOARD_OK) {
        observe_fail(ctx, ALARM_PED_DIR);
        return;
    }

    observe->stage = OBSERVE_STAGE_ACTIVE;
}

static void observe_collect_ped_faults(SystemContext *ctx) {
    uint32_t faults = 0U;

    if ((board_ped_take_faults(&faults) != BOARD_OK) || (faults == 0U)) {
        return;
    }

    if ((faults & BOARD_PED_FAULT_POWER) != 0U) {
        ctx->ped.is_powered = false;
        alarm_raise(ctx, ALARM_PED_PS);
    }
    if ((faults & BOARD_PED_FAULT_READY) != 0U) {
        alarm_raise(ctx, ALARM_PED_DIR);
    }
    if ((faults & BOARD_PED_FAULT_STATUS) != 0U) {
        alarm_raise(ctx, ALARM_PED_ST);
    }
}

static void observe_pump_writer(SystemContext *ctx) {
    ObserveContext *observe = &ctx->observe;
    NiWriterState state = ni_writer_poll(&observe->writer, &observe->stream);

    observe->committed_packet_count = ni_writer_next_packet(&observe->writer);

    switch (state) {
    case NI_WRITER_FAILED:
        observe_fail_writer(ctx);
        break;

    case NI_WRITER_FULL:
        (void)board_ped_acquisition_stop();
        observe_report_end(ctx, EVENT_NAND_FULL);
        break;

    case NI_WRITER_DONE:
        if (observe->stage == OBSERVE_STAGE_FLUSHING) {
            observe_report_end(ctx, EVENT_OBSERVE_DONE);
        }
        break;

    case NI_WRITER_IDLE:
    case NI_WRITER_RECOVERING:
    case NI_WRITER_READY:
    case NI_WRITER_FLUSHING:
    default:
        break;
    }
}

BoardStatus observe_begin(SystemContext *ctx) {
    BoardStatus status;

    if (ctx == NULL) {
        return BOARD_ERR_INVALID_ARG;
    }

    status = ni_writer_begin(&ctx->observe.writer, observe_bank_id(ctx->observe.bank));
    if (status != BOARD_OK) {
        return status;
    }

    ctx->observe.stage = OBSERVE_STAGE_ENTER;

    return BOARD_OK;
}

void observe_poll(SystemContext *ctx) {
    ObserveContext *observe;
    NiWriterState state;
    uint32_t step;
    bool more;

    if (ctx == NULL) {
        return;
    }

    observe = &ctx->observe;

    switch (observe->stage) {
    case OBSERVE_STAGE_ENTER:
        state = ni_writer_poll(&observe->writer, &observe->stream);
        if (state == NI_WRITER_READY) {
            observe_start_session(ctx);
        } else if (state == NI_WRITER_FULL) {
            observe_report_end(ctx, EVENT_NAND_FULL);
        } else if (state == NI_WRITER_FAILED) {
            observe_fail_writer(ctx);
        }
        break;

    case OBSERVE_STAGE_ACTIVE:
    case OBSERVE_STAGE_FINISHING:
        observe_collect_ped_faults(ctx);
        for (step = 0U; step < (OBSERVE_RECORDS_PER_POLL / OBSERVE_RECORDS_PER_WRITER_POLL); ++step) {
            more = observe_pump_records(ctx, OBSERVE_RECORDS_PER_WRITER_POLL);
            observe_pump_writer(ctx);
            if (!more || observe->end_reported ||
                ((observe->stage != OBSERVE_STAGE_ACTIVE) && (observe->stage != OBSERVE_STAGE_FINISHING))) {
                break;
            }
        }
        if ((observe->stage == OBSERVE_STAGE_ACTIVE) || (observe->stage == OBSERVE_STAGE_FINISHING)) {
            observe_pump_kt(ctx);
        }
        break;

    case OBSERVE_STAGE_FLUSHING:
        observe_pump_writer(ctx);
        break;

    case OBSERVE_STAGE_IDLE:
    case OBSERVE_STAGE_EXIT_CMD:
    case OBSERVE_STAGE_EXIT_FULL:
    case OBSERVE_STAGE_EXIT_ALARM:
    default:
        break;
    }
}

void observe_on_rtc_tick(SystemContext *ctx) {
    ObserveContext *observe;

    if (ctx == NULL) {
        return;
    }

    observe = &ctx->observe;

    if (observe->stage != OBSERVE_STAGE_FINISHING) {
        return;
    }

    ++observe->finish_wait_seconds;
    if (observe->finish_wait_seconds < OBSERVE_FINISH_TIMEOUT_S) {
        return;
    }

    (void)observe_science_flush_events(&observe->science, &observe->stream);
    observe_start_flush(ctx);
}

bool observe_request_params(SystemContext *ctx, uint16_t raw_params) {
    ObserveScienceParams params;

    if ((ctx == NULL) || !observe_science_decode_params(raw_params, &params)) {
        return false;
    }

    ctx->observe.observe_params = raw_params;

    if (ctx->observe.stage == OBSERVE_STAGE_ACTIVE) {
        observe_science_request_params(&ctx->observe.science, &params);
    }

    return true;
}

void observe_accept_kt(SystemContext *ctx, ObserveKtType type,
                       const uint8_t *data, uint16_t length) {
    ObserveKtLatch *latch;

    if ((ctx == NULL) || (type >= OBSERVE_KT_COUNT) || (data == NULL)) {
        return;
    }

    latch = &ctx->observe.kt[type];

    if (length > (uint16_t)TLM_PAYLOAD_MAX) {
        length = (uint16_t)TLM_PAYLOAD_MAX;
    }

    (void)memcpy(latch->data, data, length);
    latch->length = length;
    (void)memset(&latch->received_at, 0, sizeof(latch->received_at));
    (void)board_rtc_get_time(&latch->received_at);
    latch->pending = true;
}

void observe_request_final(SystemContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    observe_science_request_final(&ctx->observe.science);
    ctx->observe.finish_wait_seconds = 0U;
    ctx->observe.stage = OBSERVE_STAGE_FINISHING;
}

void observe_abort(SystemContext *ctx) {
    uint32_t type;

    if (ctx == NULL) {
        return;
    }

    (void)board_ped_acquisition_stop();
    ni_stream_discard_partial(&ctx->observe.stream);
    ctx->observe.record_count = 0U;
    ctx->observe.record_index = 0U;

    for (type = 0U; type < (uint32_t)OBSERVE_KT_COUNT; ++type) {
        ctx->observe.kt[type].pending = false;
    }
}
