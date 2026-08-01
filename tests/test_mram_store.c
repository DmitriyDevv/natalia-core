#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mram_store.h"

static void fill_test_result(MramStoreTestResult *result, uint8_t bank, uint32_t seed) {
    size_t i;

    memset(result, 0, sizeof(*result));
    result->bank = bank;
    for (i = 0U; i < TEST_MODE_BLOCK_COUNT; ++i) {
        result->nerr[i] = (seed + (uint32_t)i) & TEST_MODE_NERR_MAX;
    }
}

static void test_config_roundtrip(void) {
    MramStoreConfig in;
    MramStoreConfig out;

    memset(&in, 0, sizeof(in));
    in.mcu_pu_temp_min = -40;
    in.mcu_pu_temp_max = 85;
    in.pu_temp_min = -30;
    in.pu_temp_max = 70;
    in.ped_temp_min = -20;
    in.ped_temp_max = 60;
    in.det_temp_min = -10;
    in.det_temp_max = 50;
    in.pu_voltage_min = 3000U;
    in.pu_voltage_max = 3600U;
    in.pu_current_min = 10U;
    in.pu_current_max = 500U;
    in.ped_voltage_min = 4500U;
    in.ped_voltage_max = 5500U;
    in.ped_current_min = 5U;
    in.ped_current_max = 300U;
    in.belt_lmin = 100;
    in.belt_lmax = 900;
    in.belt_bmin = 250;
    in.ac1_rate_max = 1234U;
    in.init_rtc_time = 0x11223344U;
    in.can_control = 0x0003U;
    in.alarm_mask = 0xF00FU;
    in.config_version = 7U;

    assert(mram_store_save_config(&in) == BOARD_OK);

    memset(&out, 0, sizeof(out));
    assert(mram_store_load_config(&out) == BOARD_OK);
    assert(memcmp(&in, &out, sizeof(in)) == 0);
}

static void test_service_data_roundtrip(void) {
    MramStoreServiceData in;
    MramStoreServiceData out;

    memset(&in, 0, sizeof(in));
    in.alarm_status = 0x00FFU;
    in.nand1_full = 1U;
    in.nand2_full = 0U;
    in.last_test_status = 0xCAFED00DU;
    in.observe_session_id = 0xBEEFU;
    in.nand1_packet_count = 123456U;
    in.nand2_packet_count = 654321U;
    in.nand1_erase_count = 12U;
    in.nand2_erase_count = 34U;
    in.nand1_test_count = 5U;
    in.nand2_test_count = 6U;

    assert(mram_store_save_service_data(&in) == BOARD_OK);

    memset(&out, 0, sizeof(out));
    assert(mram_store_load_service_data(&out) == BOARD_OK);
    assert(memcmp(&in, &out, sizeof(in)) == 0);
}

static void test_test_result_per_bank(void) {
    MramStoreTestResult r1;
    MramStoreTestResult r2;
    MramStoreTestResult out;

    fill_test_result(&r1, 1U, 0x1000U);
    fill_test_result(&r2, 2U, 0x2000U);

    assert(mram_store_save_test_result(&r1) == BOARD_OK);
    assert(mram_store_save_test_result(&r2) == BOARD_OK);

    memset(&out, 0, sizeof(out));
    assert(mram_store_load_test_result(1U, &out) == BOARD_OK);
    assert(memcmp(&r1, &out, sizeof(r1)) == 0);

    memset(&out, 0, sizeof(out));
    assert(mram_store_load_test_result(2U, &out) == BOARD_OK);
    assert(memcmp(&r2, &out, sizeof(r2)) == 0);
}

/* Config + service image and both dedicated per-bank test-result regions must
 * coexist without clobbering each other. */
static void test_region_independence(void) {
    MramStoreConfig c_in;
    MramStoreConfig c_out;
    MramStoreServiceData s_in;
    MramStoreServiceData s_out;
    MramStoreTestResult t1_in;
    MramStoreTestResult t2_in;
    MramStoreTestResult t_out;

    memset(&c_in, 0, sizeof(c_in));
    c_in.alarm_mask = 0xABCDU;
    c_in.config_version = 42U;
    c_in.ac1_rate_max = 777U;

    memset(&s_in, 0, sizeof(s_in));
    s_in.alarm_status = 0x0F0FU;
    s_in.nand1_packet_count = 0x00ABCDEFU;
    s_in.nand2_test_count = 9U;

    fill_test_result(&t1_in, 1U, 0xAA00U);
    fill_test_result(&t2_in, 2U, 0xBB00U);

    assert(mram_store_save_config(&c_in) == BOARD_OK);
    assert(mram_store_save_service_data(&s_in) == BOARD_OK);
    assert(mram_store_save_test_result(&t1_in) == BOARD_OK);
    assert(mram_store_save_test_result(&t2_in) == BOARD_OK);

    memset(&c_out, 0, sizeof(c_out));
    assert(mram_store_load_config(&c_out) == BOARD_OK);
    assert(memcmp(&c_in, &c_out, sizeof(c_in)) == 0);

    memset(&s_out, 0, sizeof(s_out));
    assert(mram_store_load_service_data(&s_out) == BOARD_OK);
    assert(memcmp(&s_in, &s_out, sizeof(s_in)) == 0);

    memset(&t_out, 0, sizeof(t_out));
    assert(mram_store_load_test_result(1U, &t_out) == BOARD_OK);
    assert(memcmp(&t1_in, &t_out, sizeof(t1_in)) == 0);

    memset(&t_out, 0, sizeof(t_out));
    assert(mram_store_load_test_result(2U, &t_out) == BOARD_OK);
    assert(memcmp(&t2_in, &t_out, sizeof(t2_in)) == 0);
}

static void test_arg_validation(void) {
    MramStoreTestResult r;

    fill_test_result(&r, 3U, 0U);
    assert(mram_store_save_test_result(&r) == BOARD_ERR_INVALID_ARG);

    assert(mram_store_load_test_result(0U, &r) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_load_test_result(3U, &r) == BOARD_ERR_INVALID_ARG);

    assert(mram_store_save_test_result(NULL) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_load_test_result(1U, NULL) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_save_config(NULL) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_load_config(NULL) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_save_service_data(NULL) == BOARD_ERR_INVALID_ARG);
    assert(mram_store_load_service_data(NULL) == BOARD_ERR_INVALID_ARG);
}

int main(void) {
    test_config_roundtrip();
    test_service_data_roundtrip();
    test_test_result_per_bank();
    test_region_independence();
    test_arg_validation();

    return 0;
}
