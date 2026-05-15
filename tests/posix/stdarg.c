/*
 * POSIX conformance test: <stdarg.h>
 * Reference: docs/susv5-html/basedefs/stdarg.h.html
 *
 * POSIX (and ISO C) require: typedef va_list, macros va_start, va_arg,
 * va_end, va_copy.
 *
 * No function pins (these are all macros wrapping builtins). The
 * _macro_checks function exercises each.
 */
#include <stdarg.h>

__attribute__((unused))
static int _macro_checks(int first, ...) {
    va_list ap, ap_copy;
    va_start(ap, first);
    va_copy(ap_copy, ap);
    int x = va_arg(ap, int);
    int y = va_arg(ap_copy, int);
    va_end(ap);
    va_end(ap_copy);
    return x + y;
}
