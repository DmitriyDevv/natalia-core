#include "belt_curve.h"

#include <stddef.h>

static const BeltCurvePoint belt_curve_points[] = {
    {112, 212}, {174, 250}, {209, 250}, {255, 298}, {269, 341}, {278, 400},
    {298, 446}, {346, 454}, {386, 449}, {436, 401}, {482, 297}, {500, 376},
    {533, 308}, {800, 450}, {860, 450}, {939, 450}, {953, 450},
};

_Static_assert((sizeof(belt_curve_points) / sizeof(belt_curve_points[0])) == BELT_CURVE_POINT_COUNT,
               "belt curve point count");

const BeltCurvePoint* belt_curve_point(uint32_t index) {
    if (index >= BELT_CURVE_POINT_COUNT) {
        return NULL;
    }

    return &belt_curve_points[index];
}

bool belt_curve_inside(int16_t l_x100, int16_t b_gs1000) {
    const BeltCurvePoint* lo;
    const BeltCurvePoint* hi;
    int32_t span;
    int32_t above;
    int32_t rise;
    uint32_t i;

    if (l_x100 <= belt_curve_points[0].l_x100) {
        return b_gs1000 <= belt_curve_points[0].b_gs1000;
    }

    for (i = 1U; i < BELT_CURVE_POINT_COUNT; ++i) {
        hi = &belt_curve_points[i];

        if (l_x100 < hi->l_x100) {
            lo = &belt_curve_points[i - 1U];
            span = (int32_t)hi->l_x100 - (int32_t)lo->l_x100;
            above = (int32_t)b_gs1000 - (int32_t)lo->b_gs1000;
            rise = (int32_t)hi->b_gs1000 - (int32_t)lo->b_gs1000;

            return (above * span) <= (rise * ((int32_t)l_x100 - (int32_t)lo->l_x100));
        }
    }

    return b_gs1000 <= belt_curve_points[BELT_CURVE_POINT_COUNT - 1U].b_gs1000;
}
