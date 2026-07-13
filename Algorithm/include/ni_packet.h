#ifndef NATALIA_CORE_NI_PACKET_H
#define NATALIA_CORE_NI_PACKET_H

#include <stdint.h>

#include "state.h"

/*
 * Scientific-information (NI) packet writer and format serializers.
 *
 * Phase 4 provides only the interface. The bodies - NI packet framing per
 * Format_nauchnoy_informatsii_GS_v6, per-format serialization, and CRC16 via
 * the STM32L496 hardware CRC unit - are implemented in the scientific-
 * information phase. For now the bodies are no-ops so the OBSERVE orchestrator
 * can already call them at the correct points.
 */

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

void ni_packet_session_begin(SystemContext* ctx);
void ni_packet_session_end(SystemContext* ctx);
void ni_packet_write_format(SystemContext* ctx, NiFormatType format);

#endif /* NATALIA_CORE_NI_PACKET_H */
