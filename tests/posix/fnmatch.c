/*
 * POSIX conformance test: <fnmatch.h>
 * Reference: docs/susv5-html/basedefs/fnmatch.h.html
 * Audited: fnmatch
 * Required macros: FNM_NOMATCH, FNM_PATHNAME, FNM_NOESCAPE, FNM_PERIOD,
 *                  FNM_FILE_NAME
 * Excluded (non-POSIX): FNM_LEADING_DIR, FNM_CASEFOLD (GNU extensions)
 */
#include <fnmatch.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_fnmatch)(const char *, const char *, int) = fnmatch;

__attribute__((unused))
static void _macro_checks(void) {
    int x = FNM_NOMATCH | FNM_PATHNAME | FNM_NOESCAPE | FNM_PERIOD | FNM_FILE_NAME;
    (void)x;
}
