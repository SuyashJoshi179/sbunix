/*
 * POSIX conformance test: <math.h>
 * Reference: docs/susv5-html/basedefs/math.h.html
 *
 * Our libc declares a small subset of POSIX math.h (soft-float build, no
 * libm linked). Pin only what's declared.
 *
 * Audited: fabs, floor, ceil, round, trunc, sqrt, pow
 * Required macros: HUGE_VAL, HUGE_VALF, INFINITY, NAN, M_PI, M_E
 *
 * Excluded (not in our libc): hundreds of POSIX math funcs (sin/cos/exp/
 * log/atan/...). Auditing them is a Phase 2+ exercise once a libm exists.
 *
 * Excluded (non-POSIX): M_LOG2E, M_LN2, M_SQRT2, etc. (XSI-only).
 */
#include <math.h>

#define PIN __attribute__((unused)) static

PIN double (*_pin_fabs)(double) = fabs;
PIN double (*_pin_floor)(double) = floor;
PIN double (*_pin_ceil)(double) = ceil;
PIN double (*_pin_round)(double) = round;
PIN double (*_pin_trunc)(double) = trunc;
PIN double (*_pin_sqrt)(double) = sqrt;
PIN double (*_pin_pow)(double, double) = pow;

__attribute__((unused))
static void _macro_checks(void) {
    double x = HUGE_VAL;
    x = INFINITY;
    x = NAN;
    x = M_PI;
    x = M_E;
    (void)x;
    float y = HUGE_VALF; (void)y;
    int fc = FP_NAN | FP_INFINITE | FP_ZERO | FP_SUBNORMAL | FP_NORMAL;
    (void)fc;
}
