#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "belt_curve.h"

/* Radiation-belt boundary B_lim(L): a 17-point polyline in flash, stored in the
 * units of KT 0E00h "McIlwain parameters": L x100 and B in Gs x1000 (int16).
 * "Inside the belt" is B <= B_lim(L), the same inclusive side as the protocol's
 * "B <= Bmin". Between points the boundary is linear and compared exactly (no
 * rounding of the interpolated value); outside the table the end values hold.
 *
 * Source table (owner, 2026-11-02), L dimensionless and B in nT:
 *   L = 1.123 1.73717 2.0886 2.55214 2.6917 2.78216 2.97957 3.46219 3.85746
 *       4.35957 4.82121 5.0 5.33283 8.0 8.6 9.3876 9.53131
 *   B = 21219 25000 25000 29844 34149 40000 44574 45390 44918
 *       40120 29704 37597 30838 45000 45000 45000 45000
 * 1 Gs x1000 = 1 mGs = 100 nT, so each flash point is round(L * 100) and
 * round(B_nT / 100). Rounding moves a node by at most half the KT resolution. */

static const double source_l[BELT_CURVE_POINT_COUNT] = {
    1.123, 1.73717, 2.0886, 2.55214, 2.6917, 2.78216, 2.97957, 3.46219, 3.85746,
    4.35957, 4.82121, 5.0, 5.33283, 8.0, 8.6, 9.3876, 9.53131,
};

static const double source_b_nt[BELT_CURVE_POINT_COUNT] = {
    21219.0, 25000.0, 25000.0, 29844.0, 34149.0, 40000.0, 44574.0, 45390.0, 44918.0,
    40120.0, 29704.0, 37597.0, 30838.0, 45000.0, 45000.0, 45000.0, 45000.0,
};

static int16_t round_positive(double value) {
    return (int16_t)(value + 0.5);
}

static void table_is_source_in_kt_units(void) {
    uint32_t i;

    for (i = 0U; i < BELT_CURVE_POINT_COUNT; ++i) {
        const BeltCurvePoint* point = belt_curve_point(i);

        assert(point != NULL);
        assert(point->l_x100 == round_positive(source_l[i] * 100.0));
        assert(point->b_gs1000 == round_positive(source_b_nt[i] / 100.0));

        if (i > 0U) {
            assert(point->l_x100 > belt_curve_point(i - 1U)->l_x100);
        }
    }

    assert(belt_curve_point(BELT_CURVE_POINT_COUNT) == NULL);
}

static void node_value_is_inclusive_boundary(void) {
    uint32_t i;

    for (i = 0U; i < BELT_CURVE_POINT_COUNT; ++i) {
        const BeltCurvePoint* point = belt_curve_point(i);

        assert(belt_curve_inside(point->l_x100, point->b_gs1000));
        assert(!belt_curve_inside(point->l_x100, (int16_t)(point->b_gs1000 + 1)));
    }
}

static void boundary_between_points(void) {
    /* 1.12 -> 1.74 rising (South Atlantic Anomaly side): limit at 1.50 is 235.29. */
    assert(belt_curve_inside(150, 235));
    assert(!belt_curve_inside(150, 236));

    /* Flat 250 between 1.74 and 2.09. */
    assert(belt_curve_inside(190, 250));
    assert(!belt_curve_inside(190, 251));

    /* 2.98 -> 3.46: limit at 3.00 is 446.33. */
    assert(belt_curve_inside(300, 446));
    assert(!belt_curve_inside(300, 447));

    /* 4.36 -> 4.82 falling: limit at 4.59 is exactly 349, equality is inside. */
    assert(belt_curve_inside(459, 349));
    assert(!belt_curve_inside(459, 350));

    /* 4.82 -> 5.00 rising again: limit at 4.91 is 336.5. */
    assert(belt_curve_inside(491, 336));
    assert(!belt_curve_inside(491, 337));

    /* 5.33 -> 8.00: limit at 6.00 is 343.63. */
    assert(belt_curve_inside(600, 343));
    assert(!belt_curve_inside(600, 344));
}

static void end_values_hold_outside_table(void) {
    assert(belt_curve_inside(100, 212));
    assert(!belt_curve_inside(100, 213));
    assert(belt_curve_inside(INT16_MIN, 212));
    assert(!belt_curve_inside(INT16_MIN, 213));

    assert(belt_curve_inside(1000, 450));
    assert(!belt_curve_inside(1000, 451));
    assert(belt_curve_inside(INT16_MAX, 450));
    assert(!belt_curve_inside(INT16_MAX, 451));
}

static void int16_extremes_do_not_overflow(void) {
    assert(belt_curve_inside(INT16_MIN, INT16_MIN));
    assert(!belt_curve_inside(INT16_MAX, INT16_MAX));
    assert(belt_curve_inside(300, INT16_MIN));
    assert(!belt_curve_inside(300, INT16_MAX));
    assert(belt_curve_inside(650, INT16_MIN));
    assert(!belt_curve_inside(650, INT16_MAX));
    assert(belt_curve_inside(300, -1));
}

int main(void) {
    table_is_source_in_kt_units();
    node_value_is_inclusive_boundary();
    boundary_between_points();
    end_values_hold_outside_table();
    int16_extremes_do_not_overflow();

    return 0;
}
