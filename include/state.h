#ifndef NATALIA_CORE_STATE_H
#define NATALIA_CORE_STATE_H

#include <stdint.h>

typedef enum {
    STATE_INIT,
    STATE_DUTY,
    STATE_ERASE,
    STATE_TEST,
    STATE_OBSERVE,
    STATE_DUMP,
    STATE_ALARM,
    STATE_SHUTDOWN
} SystemState;

typedef enum {
    // Command events (КУ)
    EVENT_CMD_TELEM_REQ,
    EVENT_CMD_STATUS_REQ,
    EVENT_CMD_SET_TIME,
    EVENT_CMD_OBSERVE_START,
    EVENT_CMD_OBSERVE_CTRL,
    EVENT_CMD_DUTY,
    EVENT_CMD_DUMP,
    EVENT_CMD_SET_CFG,
    EVENT_CMD_ERASE,
    EVENT_CMD_TEST,
    EVENT_CMD_TEST_RESULT,
    EVENT_CMD_SHUTDOWN,
    EVENT_CMD_RESET_ALARM,

    // Telemetry events (КТ)
    EVENT_TLM_TIME_SYNC,
    EVENT_TLM_ORBIT,
    EVENT_TLM_ATTITUDE,
    EVENT_TLM_MAGFIELD,

    // Internal events
    EVENT_BOOT,
    EVENT_INIT_DONE,
    EVENT_INIT_FAIL,

    EVENT_RTC_1HZ,
    EVENT_PED_TRIGGER,
    EVENT_NAND_FULL,
    EVENT_ERASE_DONE,
    EVENT_TEST_DONE,
    EVENT_DUMP_DONE,

    EVENT_MASKED_ALARM_SET,
    EVENT_MASKED_ALARM_CLEAR
} EventType;

typedef struct {
    SystemState state;
    uint32_t alarm_status;
    uint32_t masked_alarm;
} SystemContext;

SystemState handle_event(SystemContext *ctx, EventType event);

#endif //NATALIA_CORE_STATE_H
