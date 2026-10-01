#ifndef NATALIA_CORE_NI_PACKET_H
#define NATALIA_CORE_NI_PACKET_H

#include <stdint.h>

#include "ni_format.h"
#include "state.h"

/*
 * Scientific-information (NI) packet writer and format serializers.
 */

void ni_packet_session_begin(SystemContext* ctx);
void ni_packet_session_end(SystemContext* ctx);
void ni_packet_write_format(SystemContext* ctx, NiFormatType format);

#endif /* NATALIA_CORE_NI_PACKET_H */
