#ifndef _STDINT_H
#define _STDINT_H

/*
 * Fixed-width integer types
 */
typedef signed char           int8_t;
typedef short                 int16_t;
typedef int                   int32_t;
typedef long                  int64_t;

typedef unsigned char         uint8_t;
typedef unsigned short        uint16_t;
typedef unsigned int          uint32_t;
typedef unsigned long         uint64_t;

/*
 * Fastest integer types (at least as wide as the fixed-width types)
 */
typedef signed char           int_fast8_t;
typedef short                 int_fast16_t;
typedef int                   int_fast32_t;
typedef long                  int_fast64_t;

typedef unsigned char         uint_fast8_t;
typedef unsigned short        uint_fast16_t;
typedef unsigned int          uint_fast32_t;
typedef unsigned long         uint_fast64_t;

/*
 * Least integer types (smallest type that can represent values)
 */
typedef signed char           int_least8_t;
typedef short                 int_least16_t;
typedef int                   int_least32_t;
typedef long                  int_least64_t;

typedef unsigned char         uint_least8_t;
typedef unsigned short        uint_least16_t;
typedef unsigned int          uint_least32_t;
typedef unsigned long         uint_least64_t;

/*
 * Integer types capable of holding pointers
 */
typedef long                  intptr_t;
typedef unsigned long         uintptr_t;

/*
 * Greatest-width integer types
 */
typedef long                  intmax_t;
typedef unsigned long         uintmax_t;

/*
 * Integer constant macros
 */
#define INT8_C(c)     (c)
#define INT16_C(c)    (c)
#define INT32_C(c)    (c)
#define INT64_C(c)    (c ## L)

#define UINT8_C(c)    (c ## U)
#define UINT16_C(c)   (c ## U)
#define UINT32_C(c)   (c ## U)
#define UINT64_C(c)   (c ## UL)

#define INTMAX_C(c)   (c ## L)
#define UINTMAX_C(c)  (c ## UL)

/*
 * Limits of exact-width integer types
 */
#define INT8_MIN      (-128)
#define INT8_MAX      (127)
#define UINT8_MAX     (255)

#define INT16_MIN     (-32768)
#define INT16_MAX     (32767)
#define UINT16_MAX    (65535)

/* -2147483648 alone doesn't fit in int (so it's typed long), which
 * breaks _Generic dispatch and trips -Wsign-conversion when INT32_MIN
 * is passed to APIs declared with int parameters. */
#define INT32_MIN     (-2147483647 - 1)
#define INT32_MAX     (2147483647)
#define UINT32_MAX    (4294967295U)

#define INT64_MIN     (-9223372036854775807L - 1)
#define INT64_MAX     (9223372036854775807L)
#define UINT64_MAX    (18446744073709551615UL)

/*
 * Limits of fast integer types
 */
#define INT_FAST8_MIN      INT8_MIN
#define INT_FAST8_MAX      INT8_MAX
#define UINT_FAST8_MAX     UINT8_MAX

#define INT_FAST16_MIN     INT16_MIN
#define INT_FAST16_MAX     INT16_MAX
#define UINT_FAST16_MAX    UINT16_MAX

#define INT_FAST32_MIN     INT32_MIN
#define INT_FAST32_MAX     INT32_MAX
#define UINT_FAST32_MAX    UINT32_MAX

#define INT_FAST64_MIN     INT64_MIN
#define INT_FAST64_MAX     INT64_MAX
#define UINT_FAST64_MAX    UINT64_MAX

/*
 * Limits of least integer types
 */
#define INT_LEAST8_MIN     INT8_MIN
#define INT_LEAST8_MAX     INT8_MAX
#define UINT_LEAST8_MAX    UINT8_MAX

#define INT_LEAST16_MIN    INT16_MIN
#define INT_LEAST16_MAX    INT16_MAX
#define UINT_LEAST16_MAX   UINT16_MAX

#define INT_LEAST32_MIN    INT32_MIN
#define INT_LEAST32_MAX    INT32_MAX
#define UINT_LEAST32_MAX   UINT32_MAX

#define INT_LEAST64_MIN    INT64_MIN
#define INT_LEAST64_MAX    INT64_MAX
#define UINT_LEAST64_MAX   UINT64_MAX

/*
 * Limits of pointer integer types
 */
#define INTPTR_MIN    (-9223372036854775807L - 1)
#define INTPTR_MAX    (9223372036854775807L)
#define UINTPTR_MAX   (18446744073709551615UL)

/*
 * Limits of maximum-width integer types
 */
#define INTMAX_MIN    INT64_MIN
#define INTMAX_MAX    INT64_MAX
#define UINTMAX_MAX   UINT64_MAX

/*
 * Limits of other integer types
 */
#define PTRDIFF_MIN   INTPTR_MIN
#define PTRDIFF_MAX   INTPTR_MAX

#define SIZE_MAX      UINTPTR_MAX

#endif
