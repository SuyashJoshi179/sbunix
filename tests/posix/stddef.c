/*
 * POSIX conformance test: <stddef.h>
 * Reference: docs/susv5-html/basedefs/stddef.h.html
 *
 * Required typedefs: size_t, ptrdiff_t, wchar_t
 * Required macros: NULL, offsetof
 *
 * Excluded (C11): max_align_t — our libc doesn't define it.
 */
#include <stddef.h>

struct _t { int a; double b; };

__attribute__((unused))
static void _type_checks(void) {
    size_t   s = 0;     (void)s;
    ptrdiff_t p = 0;    (void)p;
    wchar_t  w = 0;     (void)w;
    void   *n = NULL;   (void)n;

    size_t off = offsetof(struct _t, b);
    (void)off;
}
