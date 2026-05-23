#ifndef NATALIA_CORE_ALARM_H
#define NATALIA_CORE_ALARM_H

#include <stdint.h>

#define ALARM_MC_TEMP      (1UL << 0)
#define ALARM_PU_TEMP      (1UL << 1)
#define ALARM_PED_TEMP     (1UL << 2)
#define ALARM_BD_TEMP      (1UL << 3)
#define ALARM_PU_VOLT      (1UL << 4)
#define ALARM_PU_CURR      (1UL << 5)
#define ALARM_PED_VOLT     (1UL << 6)
#define ALARM_PED_CURR     (1UL << 7)
#define ALARM_PED_PS       (1UL << 8)
#define ALARM_PED_DIR      (1UL << 9)
#define ALARM_PED_ST       (1UL << 10)
#define ALARM_NAND_PS      (1UL << 11)
#define ALARM_NAND_PR      (1UL << 12)
#define ALARM_USB_VBUS     (1UL << 13)
#define ALARM_USB_PR       (1UL << 14)
#define ALARM_MRAM         (1UL << 15)

#define ALARM_NON_MASKABLE_MASK \
(ALARM_NAND_PS | ALARM_NAND_PR | ALARM_USB_PR)

#define ALARM_ALL_MASK \
(ALARM_MC_TEMP  | ALARM_PU_TEMP   | ALARM_PED_TEMP | ALARM_BD_TEMP  | \
ALARM_PU_VOLT  | ALARM_PU_CURR   | ALARM_PED_VOLT | ALARM_PED_CURR | \
ALARM_PED_PS   | ALARM_PED_DIR   | ALARM_PED_ST   | ALARM_NAND_PS  | \
ALARM_NAND_PR  | ALARM_USB_VBUS  | ALARM_USB_PR   | ALARM_MRAM)

static inline uint32_t alarm_sanitize_mask(uint32_t mask)
{
    mask &= ALARM_ALL_MASK;
    mask |= ALARM_NON_MASKABLE_MASK;
    return mask;
}

#endif /* NATALIA_CORE_ALARM_H */