#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actions.h"
#include "alarm.h"
#include "board_comm_stub.h"
#include "board_stub.h"
#include "event_queue.h"
#include "mram_store.h"
#include "state.h"
#include "transport.h"

#define KU_SET_CFG_MSG_ID (0x0F07U)
#define SET_CFG_SIZE      (68U)
#define ADDR_NA           (0x1EU)
#define ADDR_BVS          (0x05U)

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
}

static void put24(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static void put16i(uint8_t* p, int16_t v) {
    put16(p, (uint16_t)v);
}

/* Builds a fully-populated, reserved-bit-clean 68-byte CMD_SET_CFG payload with
 * a distinct recognizable value in every field so offsets/endianness/sign can be
 * checked. write_control selects session-id (bit0) and NAND2 erase (bit4). */
static void build_reference_payload(uint8_t* p) {
    memset(p, 0, SET_CFG_SIZE);

    put16(&p[0], 0x0011U);     /* write_control: bit0 + bit4 */
    put16i(&p[2], -40);
    put16i(&p[4], 85);
    put16i(&p[6], -30);
    put16i(&p[8], 70);
    put16i(&p[10], -20);
    put16i(&p[12], 60);
    put16i(&p[14], -50);
    put16i(&p[16], 55);
    put16(&p[18], 3000U);
    put16(&p[20], 3600U);
    put16(&p[22], 100U);
    put16(&p[24], 500U);
    put16(&p[26], 4000U);
    put16(&p[28], 4200U);
    put16(&p[30], 200U);
    put16(&p[32], 800U);
    put16i(&p[34], -5);
    put16i(&p[36], 12);
    put16i(&p[38], -3);
    put16(&p[40], 1234U);
    put16(&p[42], 0x02EEU);    /* init_rtc_time_ms */
    put32(&p[44], 0x11223344UL);
    put16(&p[48], 0xABCDU);
    put24(&p[50], 0x123456UL);
    put24(&p[53], 0x0ABCDEUL);
    put16(&p[56], 0x1111U);
    put16(&p[58], 0x2222U);
    put16(&p[60], 0x3333U);
    put16(&p[62], 0x4444U);
    put16(&p[64], 0x00FFU);
    put16(&p[66], 0x0003U);    /* can_control: bit0 + bit1 */
}

static void assert_reference_fields(const CmdSetConfig* c) {
    assert(c->write_control == 0x0011U);
    assert(c->mcu_pu_temp_min == -40);
    assert(c->mcu_pu_temp_max == 85);
    assert(c->pu_temp_min == -30);
    assert(c->pu_temp_max == 70);
    assert(c->ped_temp_min == -20);
    assert(c->ped_temp_max == 60);
    assert(c->det_temp_min == -50);
    assert(c->det_temp_max == 55);
    assert(c->pu_voltage_min == 3000U);
    assert(c->pu_voltage_max == 3600U);
    assert(c->pu_current_min == 100U);
    assert(c->pu_current_max == 500U);
    assert(c->ped_voltage_min == 4000U);
    assert(c->ped_voltage_max == 4200U);
    assert(c->ped_current_min == 200U);
    assert(c->ped_current_max == 800U);
    assert(c->belt_lmin == -5);
    assert(c->belt_lmax == 12);
    assert(c->belt_bmin == -3);
    assert(c->ac1_rate_max == 1234U);
    assert(c->init_rtc_time_ms == 0x02EEU);
    assert(c->init_rtc_time == 0x11223344UL);
    assert(c->observe_session_id == 0xABCDU);
    assert(c->nand1_packet_count == 0x123456UL);
    assert(c->nand2_packet_count == 0x0ABCDEUL);
    assert(c->nand1_erase_count == 0x1111U);
    assert(c->nand2_erase_count == 0x2222U);
    assert(c->nand1_test_count == 0x3333U);
    assert(c->nand2_test_count == 0x4444U);
    assert(c->alarm_mask == 0x00FFU);
    assert(c->can_control == 0x0003U);
}

static void payload_is_parsed_field_for_field(void) {
    SystemContext ctx;
    SystemEvent event;
    uint8_t payload[SET_CFG_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    build_reference_payload(payload);

    board_comm_stub_inject_rx(KU_SET_CFG_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SET_CFG_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 1U);
    assert(system_event_queue_pop(&event));
    assert(event.type == EVENT_CMD_SET_CFG);
    assert(event.msg_id == KU_SET_CFG_MSG_ID);

    assert_reference_fields(&event.command.set_config);
}

static void reserved_write_control_bit_is_rejected(void) {
    SystemContext ctx;
    uint8_t payload[SET_CFG_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    build_reference_payload(payload);
    put16(&payload[0], 0x0080U); /* reserved bit 7 set */

    board_comm_stub_inject_rx(KU_SET_CFG_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SET_CFG_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

static void reserved_can_control_bit_is_rejected(void) {
    SystemContext ctx;
    uint8_t payload[SET_CFG_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    build_reference_payload(payload);
    put16(&payload[66], 0x0004U); /* reserved bit 2 set */

    board_comm_stub_inject_rx(KU_SET_CFG_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SET_CFG_SIZE);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

static void wrong_length_is_rejected(void) {
    SystemContext ctx;
    uint8_t payload[SET_CFG_SIZE];

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    build_reference_payload(payload);

    board_comm_stub_inject_rx(KU_SET_CFG_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SET_CFG_SIZE - 1U);

    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_get_count() == 0U);
}

static void make_reference_event(SystemEvent* event) {
    uint8_t payload[SET_CFG_SIZE];
    SystemContext ctx;

    memset(&ctx, 0, sizeof(ctx));
    board_stub_reset_all();
    system_event_queue_init();
    board_comm_stub_reset();
    build_reference_payload(payload);

    board_comm_stub_inject_rx(KU_SET_CFG_MSG_ID, ADDR_BVS, ADDR_NA,
                              payload, SET_CFG_SIZE);
    assert(transport_poll(&ctx, 0U) == BOARD_OK);
    assert(system_event_queue_pop(event));
    assert(event->type == EVENT_CMD_SET_CFG);
}

/* apply_config + write_mram must persist the whole config image, preserve the
 * firmware-only config_version, and update only the counters whose write-control
 * bit is set. The reference payload sets bit0 (session id) and bit4 (NAND2 erase). */
static void apply_and_write_persists_config_and_gated_counters(void) {
    SystemContext ctx;
    SystemEvent event;
    MramStoreConfig config_baseline;
    MramStoreServiceData service_baseline;
    MramStoreConfig config_loaded;
    MramStoreServiceData service_loaded;

    make_reference_event(&event);

    memset(&config_baseline, 0, sizeof(config_baseline));
    config_baseline.config_version = 7U;
    config_baseline.alarm_mask = 0x0000U;
    assert(mram_store_save_config(&config_baseline) == BOARD_OK);

    memset(&service_baseline, 0, sizeof(service_baseline));
    service_baseline.observe_session_id = 0x1000U;
    service_baseline.nand1_packet_count = 0x00A00000UL;
    service_baseline.nand2_packet_count = 0x00B00000UL;
    service_baseline.nand1_erase_count = 0xC000U;
    service_baseline.nand2_erase_count = 0xD000U;
    service_baseline.nand1_test_count = 0xE000U;
    service_baseline.nand2_test_count = 0xF000U;
    assert(mram_store_save_service_data(&service_baseline) == BOARD_OK);

    memset(&ctx, 0, sizeof(ctx));
    assert(action_apply_config(&ctx, &event) == ACTION_OK);
    assert(ctx.alarm_mask == alarm_sanitize_mask(0x00FFU));
    assert(ctx.observe_session_id == 0xABCDU);

    assert(action_write_mram(&ctx, &event) == ACTION_OK);

    memset(&config_loaded, 0, sizeof(config_loaded));
    assert(mram_store_load_config(&config_loaded) == BOARD_OK);
    assert(config_loaded.mcu_pu_temp_min == -40);
    assert(config_loaded.det_temp_max == 55);
    assert(config_loaded.pu_voltage_min == 3000U);
    assert(config_loaded.ped_current_max == 800U);
    assert(config_loaded.belt_lmin == -5);
    assert(config_loaded.ac1_rate_max == 1234U);
    assert(config_loaded.init_rtc_time == 0x11223344UL);
    assert(config_loaded.init_rtc_time_ms == 0x02EEU);
    assert(config_loaded.can_control == 0x0003U);
    assert(config_loaded.alarm_mask == alarm_sanitize_mask(0x00FFU));
    assert(config_loaded.config_version == 7U);

    memset(&service_loaded, 0, sizeof(service_loaded));
    assert(mram_store_load_service_data(&service_loaded) == BOARD_OK);
    /* selected by write_control */
    assert(service_loaded.observe_session_id == 0xABCDU);
    assert(service_loaded.nand2_erase_count == 0x2222U);
    /* not selected -> baseline preserved */
    assert(service_loaded.nand1_packet_count == 0x00A00000UL);
    assert(service_loaded.nand2_packet_count == 0x00B00000UL);
    assert(service_loaded.nand1_erase_count == 0xC000U);
    assert(service_loaded.nand1_test_count == 0xE000U);
    assert(service_loaded.nand2_test_count == 0xF000U);
}

/* §15.4 / mode_alarm.md §13.5: the new config must go live in RAM only after
 * both MRAM copies are written. On a write failure the command is rejected and
 * ctx keeps its previous alarm_mask / can_control / observe_session_id. */
static void config_not_applied_when_mram_write_fails(void) {
    SystemContext ctx;
    SystemEvent event;

    make_reference_event(&event);

    memset(&ctx, 0, sizeof(ctx));
    ctx.state = STATE_DUTY;
    ctx.can_control = 0x1234U;
    ctx.alarm_mask = 0x00F0U;
    ctx.observe_session_id = 0x9999U;

    board_stub_set_mram_write_fail(true);
    assert(handle_event(&ctx, &event) == STATE_DUTY);
    board_stub_set_mram_write_fail(false);

    assert(ctx.can_control == 0x1234U);
    assert(ctx.alarm_mask == 0x00F0U);
    assert(ctx.observe_session_id == 0x9999U);
}

int main(void) {
    payload_is_parsed_field_for_field();
    reserved_write_control_bit_is_rejected();
    reserved_can_control_bit_is_rejected();
    wrong_length_is_rejected();
    apply_and_write_persists_config_and_gated_counters();
    config_not_applied_when_mram_write_fails();

    return 0;
}
