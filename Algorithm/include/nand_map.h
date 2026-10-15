#ifndef NATALIA_CORE_NAND_MAP_H
#define NATALIA_CORE_NAND_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "board_api.h"

BoardStatus nand_map_load(uint8_t bank_id);
BoardStatus nand_map_rebuild(uint8_t bank_id);
BoardStatus nand_map_save(uint8_t bank_id);
bool nand_map_mram_failed(void);

#endif /* NATALIA_CORE_NAND_MAP_H */
