#pragma once

/* Declarations only. SBUnix builds soft-float (-march=rv64imac, -mabi=lp64);
 * no math implementations are linked. Code that calls these will see an
 * unresolved-symbol link error rather than silent wrong answers. When the
 * MicroPython port lands with MICROPY_FLOAT_IMPL_NONE this header is just
 * a declaration backstop; later phases may add a soft-float libm. */

#define HUGE_VAL    (__builtin_huge_val())
#define HUGE_VALF   (__builtin_huge_valf())
#define INFINITY    (__builtin_inff())
#define NAN         (__builtin_nanf(""))

#define M_E         2.7182818284590452354
#define M_LOG2E     1.4426950408889634074
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_2_SQRTPI  1.12837916709551257390
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
double sqrt(double x);
double pow(double base, double exp);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double fmod(double x, double y);
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double copysign(double x, double y);

float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float sqrtf(float x);
float powf(float x, float y);
float expf(float x);
float logf(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float atan2f(float y, float x);
float fmodf(float x, float y);

int   isnan(double x);
int   isinf(double x);
int   isfinite(double x);
int   signbit(double x);
