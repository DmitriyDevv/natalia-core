#ifndef NATALIA_CORE_STATE_H
#define NATALIA_CORE_STATE_H

#include <stdbool.h>
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

typedef enum {
    NAND_BANK_NONE = 0,
    NAND_BANK_1 = 1,
    NAND_BANK_2 = 2
} NandBank;

typedef enum {
    POWER_AFTER_DONE_OFF = 0,
    POWER_AFTER_DONE_KEEP = 1
} PowerAfterDone;

typedef enum {
    ERASE_STAGE_IDLE,
    ERASE_STAGE_ENTER,
    ERASE_STAGE_START,
    ERASE_STAGE_WAIT,
    ERASE_STAGE_FINISH_OK,
    ERASE_STAGE_FINISH_CMD,
    ERASE_STAGE_FINISH_ALARM
} EraseStage;

typedef enum {
    TEST_STAGE_IDLE,
    TEST_STAGE_ENTER,
    TEST_STAGE_WRITE,
    TEST_STAGE_READ,
    TEST_STAGE_COMPARE,
    TEST_STAGE_SAVE,
    TEST_STAGE_ERASE,
    TEST_STAGE_FINISH_OK,
    TEST_STAGE_FINISH_CMD,
    TEST_STAGE_FINISH_ALARM
} TestStage;

typedef enum {
    DUMP_STAGE_IDLE,
    DUMP_STAGE_ENTER,
    DUMP_STAGE_READ,
    DUMP_STAGE_SEND,
    DUMP_STAGE_CHECK,
    DUMP_STAGE_FINISH_OK,
    DUMP_STAGE_FINISH_CMD,
    DUMP_STAGE_FINISH_ALARM
} DumpStage;

typedef enum {
    OBSERVE_STAGE_IDLE,
    OBSERVE_STAGE_ENTER,
    OBSERVE_STAGE_ACTIVE,
    OBSERVE_STAGE_EXIT_CMD,
    OBSERVE_STAGE_EXIT_FULL,
    OBSERVE_STAGE_EXIT_ALARM
} ObserveStage;

typedef enum {
    SHUTDOWN_STAGE_IDLE,
    SHUTDOWN_STAGE_STOP_ACTIVE,
    SHUTDOWN_STAGE_SAVE_SERVICE_DATA,
    SHUTDOWN_STAGE_POWER_OFF,
    SHUTDOWN_STAGE_DONE,
    SHUTDOWN_STAGE_ERROR
} ShutdownStage;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
} CmdErase;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    uint32_t test_mask;
} CmdTest;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    uint32_t start_address;
    uint32_t size;
} CmdDump;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    uint32_t acquisition_period_ticks;
} CmdObserveStart;

typedef struct {
    bool inhibit_enabled;
    bool sleep_enabled;
} CmdObserveCtrl;

typedef struct {
    PowerAfterDone power_after_done;
} CmdDuty;

typedef struct {
    uint64_t time_ticks;
} CmdSetTime;

typedef struct {
    uint32_t config_id;
} CmdSetConfig;

typedef union {
    CmdErase erase;
    CmdTest test;
    CmdDump dump;
    CmdObserveStart observe_start;
    CmdObserveCtrl observe_ctrl;
    CmdDuty duty;
    CmdSetTime set_time;
    CmdSetConfig set_config;
} CommandPayload;

typedef struct {
    EventType type;
    uint32_t msg_id;
    CommandPayload command;
} SystemEvent;

typedef struct {
    NandBank bank;
    bool is_powered;
    bool is_connected;
    bool is_full;
} NandRuntimeState;

typedef struct {
    bool is_powered;
    bool inhibit_enabled;
    bool sleep_enabled;
    uint32_t status;
} PedRuntimeState;

typedef struct {
    bool is_ready;
    uint32_t bytes_written;
} UsbRuntimeState;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    EraseStage stage;
    uint32_t current_address;
    bool finish_requested;
} EraseContext;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    TestStage stage;
    uint32_t test_mask;
    uint32_t result_status;
    bool result_valid;
    bool finish_requested;
    SystemState finish_target_state;
} TestContext;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    DumpStage stage;
    uint32_t start_address;
    uint32_t size;
    uint32_t bytes_done;
    uint32_t last_dumped_packet;
    bool finish_requested;
    SystemState finish_target_state;
} DumpContext;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    ObserveStage stage;
    uint32_t acquisition_period_ticks;
    uint32_t events_written;
    bool registration_enabled;
    bool finish_requested;
    SystemState finish_target_state;
} ObserveContext;

typedef struct {
    ShutdownStage stage;
    SystemState source_state;
    bool service_data_save_failed;
    bool power_off_failed;
} ShutdownContext;

typedef struct {
    SystemState state;
    SystemState previous_state;
    uint32_t alarm_status;
    uint32_t alarm_mask;
    uint32_t masked_alarm;
    NandRuntimeState nand1;
    NandRuntimeState nand2;
    PedRuntimeState ped;
    UsbRuntimeState usb;
    EraseContext erase;
    TestContext test;
    DumpContext dump;
    ObserveContext observe;
    ShutdownContext shutdown;
} SystemContext;

SystemState handle_event(SystemContext *ctx, const SystemEvent *event);

#endif //NATALIA_CORE_STATE_H
