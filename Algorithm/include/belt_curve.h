#ifndef NATALIA_CORE_BELT_CURVE_H
#define NATALIA_CORE_BELT_CURVE_H

#include <stdbool.h>
#include <stdint.h>

#define BELT_CURVE_POINT_COUNT 17U

typedef struct {
    int16_t l_x100;
    int16_t b_gs1000;
} BeltCurvePoint;

const BeltCurvePoint* belt_curve_point(uint32_t index);
bool belt_curve_inside(int16_t l_x100, int16_t b_gs1000);

#endif /* NATALIA_CORE_BELT_CURVE_H */
