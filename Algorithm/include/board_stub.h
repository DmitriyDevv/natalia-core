#ifndef NATALIA_CORE_BOARD_STUB_H
#define NATALIA_CORE_BOARD_STUB_H

#include <stdbool.h>

/* Host-test control hooks for the Board_API stub (NATALIA_USE_BOARD_STUBS).
 * These exist only to let tests simulate hardware failures deterministically;
 * they are not part of the real Board_API. */

/* When enabled, board_mram_write returns BOARD_ERR_IO instead of storing data.
 * Used to exercise MRAM save-failure paths. Defaults to disabled. */
void board_stub_set_mram_write_fail(bool fail);

#endif /* NATALIA_CORE_BOARD_STUB_H */
