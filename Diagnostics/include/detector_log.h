#ifndef NATALIA_DETECTOR_LOG_H
#define NATALIA_DETECTOR_LOG_H

#include "unican.h"

void detector_log_rx(const UnicanMessage *message);

void detector_log_tx(const UnicanMessage *message);

#endif /* NATALIA_DETECTOR_LOG_H */