#ifndef NATALIA_CORE_BOARD_STUB_H
#define NATALIA_CORE_BOARD_STUB_H

#include <stdbool.h>
#include <stdint.h>

#include "board_api.h"

/* Host-test control hooks for the Board_API stub (NATALIA_USE_BOARD_STUBS).
 * These exist only to let tests simulate hardware failures deterministically;
 * they are not part of the real Board_API. */

/* Returns the whole stub model to its initial state: NAND filled with 0xFF,
 * packet counters and bank-full flags cleared, bank power off, MRAM (including
 * the test-result images) zeroed, and every injected value (temperatures, power
 * monitors, PED power, RTC time, event counters, failure flags) back to its
 * default. It also calls board_comm_stub_reset(), so the receive slot, the TX
 * ring and any armed send failure are cleared too. Call it from begin_test() so
 * tests do not inherit state from earlier check functions in the same binary. */
void board_stub_reset_all(void);

/* When enabled, board_mram_write returns BOARD_ERR_IO instead of storing data.
 * Used to exercise MRAM save-failure paths. Defaults to disabled. */
void board_stub_set_mram_write_fail(bool fail);

/* Controls the is_valid flag returned by board_mram_check_crc for an MRAM copy
 * (1 or 2), set independently per copy. Used to exercise the backup-copy restore
 * path and the ALARM_MRAM condition. Defaults to valid for both copies. */
void board_stub_set_mram_crc_valid(uint8_t copy_id, bool valid);

/* Controls the is_valid flag returned by board_mram_read_test_result for a NAND
 * bank (1 or 2). Used to exercise the "bank never tested" path. Defaults to
 * valid for both banks. */
void board_stub_set_test_result_valid(uint8_t nand_bank, bool valid);

/* Sets the pending count returned by the next board_rtc_take_1hz_events /
 * board_ped_take_trigger_events call; the take resets it to zero (drain
 * semantics). Used to exercise flight-loop hardware-event collection. */
void board_stub_set_rtc_1hz_events(uint32_t count);
void board_stub_set_ped_trigger_events(uint32_t count);

/* Injects the digital-temp / power-monitor readings and the PED power state used
 * by the 20 s alarm monitor. Temperature is milli-degC; voltage mV; current uA.
 * `valid`/`ready` false makes the monitor skip that parameter. */
void board_stub_set_digital_temp(BoardTempSensorId sensor, int32_t milli_c, bool valid);
void board_stub_set_power_monitor(BoardPowerMonitorId monitor, uint32_t mv, int32_t ua, bool ready);
void board_stub_set_ped_powered(bool powered);

/* Overrides the value board_nand_is_powered reports for a bank (1 or 2),
 * winning over the model flag set by board_nand_power_on/off until the next
 * board_stub_reset_all. Lets a test model "power switched on, but no power-good
 * confirmation" to exercise the ALARM_NAND_PS path. */
void board_stub_set_nand_powered(uint8_t bank_id, bool powered);

/* Sets the value returned by board_rtc_get_time. */
void board_stub_set_rtc_time(uint32_t seconds, uint16_t milliseconds);

#endif /* NATALIA_CORE_BOARD_STUB_H */
