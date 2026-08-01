#ifndef NATALIA_CORE_STATE_H
#define NATALIA_CORE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "instrument_time.h"

#include "dump_mode_config.h"
#include "test_mode_config.h"

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

    EVENT_TLM_TIME_SYNC,
    EVENT_TLM_ORBIT,
    EVENT_TLM_MAGFIELD,

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
    uint32_t requested_packet_count;
    bool dump_all;
} CmdDump;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    uint16_t observe_params;
    uint16_t trigger_config;
    uint32_t acquisition_period_ticks;
    bool ped_power_enabled;
    bool ped_sleep_enabled;
    bool registration_enabled;
    bool ped_power_after_full;
    bool ped_sleep_after_full;
} CmdObserveStart;

typedef struct {
    bool inhibit_enabled;
    bool sleep_enabled;
    bool ped_power_enabled;
    bool registration_enabled;
    uint16_t observe_params;
    uint16_t trigger_config;
} CmdObserveCtrl;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    bool ped_power_enabled;
    bool ped_sleep_enabled;
} CmdDuty;

typedef struct {
    InstrumentTime time;
} CmdSetTime;

typedef struct {
    uint16_t write_control;
    int16_t mcu_pu_temp_min;
    int16_t mcu_pu_temp_max;
    int16_t pu_temp_min;
    int16_t pu_temp_max;
    int16_t ped_temp_min;
    int16_t ped_temp_max;
    int16_t det_temp_min;
    int16_t det_temp_max;
    uint16_t pu_voltage_min;
    uint16_t pu_voltage_max;
    uint16_t pu_current_min;
    uint16_t pu_current_max;
    uint16_t ped_voltage_min;
    uint16_t ped_voltage_max;
    uint16_t ped_current_min;
    uint16_t ped_current_max;
    int16_t belt_lmin;
    int16_t belt_lmax;
    int16_t belt_bmin;
    uint16_t ac1_rate_max;
    uint32_t init_rtc_time;
    uint16_t observe_session_id;
    uint32_t nand1_packet_count;
    uint32_t nand2_packet_count;
    uint16_t nand1_erase_count;
    uint16_t nand2_erase_count;
    uint16_t nand1_test_count;
    uint16_t nand2_test_count;
    uint16_t alarm_mask;
    uint16_t can_control;
} CmdSetConfig;

typedef struct {
    NandBank bank;
    uint8_t mram_copy;
} CmdTestResult;

typedef union {
    CmdErase erase;
    CmdTest test;
    CmdDump dump;
    CmdObserveStart observe_start;
    CmdObserveCtrl observe_ctrl;
    CmdDuty duty;
    CmdSetTime set_time;
    CmdSetConfig set_config;
    CmdTestResult test_result;
} CommandPayload;

typedef struct {
    EventType type;
    uint32_t msg_id;
    uint8_t tlm_slot;
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
    bool operation_failed;
    bool finish_requested;
} EraseContext;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    TestStage stage;
    uint32_t test_mask;
    uint32_t current_address;
    uint32_t block_index;
    uint32_t packet_in_block;
    uint32_t total_blocks;
    uint32_t result_status;
    uint32_t total_errors;
    uint32_t failed_address;
    uint32_t nerr[TEST_MODE_BLOCK_COUNT];
    uint8_t write_buffer[TEST_MODE_PACKET_SIZE];
    uint8_t read_buffer[TEST_MODE_PACKET_SIZE];
    bool result_valid;
    bool operation_failed;
    bool finish_requested;
    bool final_erase;
    bool write_started;
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
    uint32_t packet_size;
    uint32_t send_offset;
    uint32_t usb_retry_count;
    uint8_t packet_buffer[DUMP_MODE_PACKET_SIZE];
    bool operation_failed;
    bool finish_requested;
    SystemState finish_target_state;
} DumpContext;

#define TLM_PAYLOAD_MAX 128U

typedef struct {
    uint8_t data[TLM_PAYLOAD_MAX];
    uint16_t length;
    bool valid;
} KtLatch;

typedef struct {
    NandBank bank;
    PowerAfterDone power_after_done;
    ObserveStage stage;
    uint32_t acquisition_period_ticks;
    uint32_t events_written;
    uint32_t packet_index;
    uint32_t committed_packet_count;
    uint8_t packet_buffer[DUMP_MODE_PACKET_SIZE];
    bool registration_enabled;
    bool finish_requested;
    bool pending_write;
    bool write_active;
    bool operation_failed;
    SystemState finish_target_state;
    uint16_t observe_params;
    uint16_t trigger_config;
    uint8_t observe_mode_number;
    uint32_t format_number;
    uint32_t seconds_elapsed;
    bool first_tick;
    bool telem_pending;
    uint8_t events_mode;
    uint8_t events_nmax_sel;
    uint8_t spectrum_mode;
    uint8_t spectrum_nhist_sel;
    uint8_t last_emitted_format;
    KtLatch kt_sync_orbit_attitude;
    KtLatch kt_geomagnetic;
    KtLatch kt_mcilwain;
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
    uint16_t observe_session_id;
    uint16_t can_control;
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

SystemState handle_event(SystemContext* ctx, const SystemEvent* event);

#endif
