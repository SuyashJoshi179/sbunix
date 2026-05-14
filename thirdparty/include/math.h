#ifndef _MATH_H
#define _MATH_H

#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define HUGE_VAL (__builtin_huge_val())

#define isinf(x)   __builtin_isinf(x)
#define isnan(x)   __builtin_isnan(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)

static inline double fabs(double x) { return __builtin_fabs(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline double pow(double x, double y) { return __builtin_pow(x, y); }
static inline double fmod(double x, double y) { return __builtin_fmod(x, y); }
static inline double log(double x) { return __builtin_log(x); }
static inline double log2(double x) { return __builtin_log2(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double exp(double x) { return __builtin_exp(x); }
static inline double frexp(double x, int *e) { return __builtin_frexp(x, e); }
static inline double ldexp(double x, int e) { return __builtin_ldexp(x, e); }
static inline double modf(double x, double *i) { return __builtin_modf(x, i); }
static inline double round(double x) { return __builtin_round(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline double copysign(double x, double y) { return __builtin_copysign(x, y); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline float powf(float x, float y) { return __builtin_powf(x, y); }
static inline float roundf(float x) { return __builtin_roundf(x); }
static inline float truncf(float x) { return __builtin_truncf(x); }
static inline float copysignf(float x, float y) { return __builtin_copysignf(x, y); }

#endif
