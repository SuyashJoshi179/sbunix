/*
 * POSIX conformance test: <stdbool.h>
 * Reference: docs/susv5-html/basedefs/stdbool.h.html
 *
 * POSIX (and ISO C99) require: bool, true, false, __bool_true_false_are_defined.
 */
#include <stdbool.h>

__attribute__((unused))
static void _macro_checks(void) {
    bool b = true;
    b = false;
    (void)b;
    int x = __bool_true_false_are_defined;
    (void)x;
}
