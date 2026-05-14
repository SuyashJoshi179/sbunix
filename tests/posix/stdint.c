/*
 * POSIX conformance test: <stdint.h>
 * Reference: docs/susv5-html/basedefs/stdint.h.html
 *
 * Required typedefs: int8_t, int16_t, int32_t, int64_t (and unsigned
 * variants), int_fast*_t, int_least*_t, intmax_t, intptr_t (and
 * unsigned variants).
 *
 * Required macros: INT*_MIN/MAX family.
 */
#include <stdint.h>

__attribute__((unused))
static void _type_checks(void) {
    int8_t  a = 0;  (void)a;
    int16_t b = 0;  (void)b;
    int32_t c = 0;  (void)c;
    int64_t d = 0;  (void)d;
    uint8_t  e = 0; (void)e;
    uint16_t f = 0; (void)f;
    uint32_t g = 0; (void)g;
    uint64_t h = 0; (void)h;

    int_fast8_t  i = 0;  (void)i;
    int_fast16_t j = 0;  (void)j;
    int_fast32_t k = 0;  (void)k;
    int_fast64_t l = 0;  (void)l;

    int_least8_t  m = 0; (void)m;
    int_least16_t n = 0; (void)n;
    int_least32_t o = 0; (void)o;
    int_least64_t p = 0; (void)p;

    intmax_t  q = 0; (void)q;
    uintmax_t r = 0; (void)r;
    intptr_t  s = 0; (void)s;
    uintptr_t t = 0; (void)t;
}

__attribute__((unused))
static void _macro_checks(void) {
    long long x = INT8_MIN; x = INT8_MAX;
    x = INT16_MIN; x = INT16_MAX;
    x = INT32_MIN; x = INT32_MAX;
    x = INT64_MIN; x = INT64_MAX;
    unsigned long long y = UINT8_MAX;
    y = UINT16_MAX; y = UINT32_MAX; y = UINT64_MAX;
    (void)x; (void)y;
}
