/*
 * POSIX conformance test: <assert.h>
 * Reference: docs/susv5-html/basedefs/assert.h.html
 *
 * POSIX requires:
 *   - assert macro
 *   - static_assert macro (C11)
 *   - NDEBUG controls the assert macro's expansion
 *
 * No function pins; assert is a macro. The _macro_checks function compiles
 * if both forms are usable. NDEBUG-mode test is implicit (we don't define
 * NDEBUG so assert evaluates its argument).
 */
#include <assert.h>

__attribute__((unused))
static void _macro_checks(int x) {
    assert(x == x);
    static_assert(sizeof(int) >= 2, "int must be at least 16 bits per ISO C");
}
