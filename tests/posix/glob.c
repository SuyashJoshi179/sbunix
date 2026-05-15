/*
 * POSIX conformance test: <glob.h>
 * Reference: docs/susv5-html/basedefs/glob.h.html
 * Audited: glob, globfree
 * Required struct glob_t: gl_pathc (size_t), gl_pathv (char **), gl_offs (size_t)
 * Required macros: GLOB_ERR, GLOB_MARK, GLOB_NOSORT, GLOB_DOOFFS, GLOB_NOCHECK,
 *                  GLOB_APPEND, GLOB_NOESCAPE, GLOB_ABORTED, GLOB_NOSPACE,
 *                  GLOB_NOMATCH, GLOB_NOSYS
 * Excluded (non-POSIX): GLOB_PERIOD, GLOB_TILDE, GLOB_BRACE, GLOB_NOMAGIC,
 *                       GLOB_ONLYDIR (GNU extensions)
 */
#include <glob.h>

#define PIN __attribute__((unused)) static

PIN int  (*_pin_glob)(const char *, int,
                      int (*)(const char *, int),
                      glob_t *) = glob;
PIN void (*_pin_globfree)(glob_t *) = globfree;

__attribute__((unused))
static void _struct_fields(void) {
    glob_t g;
    __builtin_memset(&g, 0, sizeof g);
    (void)g.gl_pathc; (void)g.gl_pathv; (void)g.gl_offs;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = GLOB_ERR | GLOB_MARK | GLOB_NOSORT | GLOB_DOOFFS
          | GLOB_NOCHECK | GLOB_APPEND | GLOB_NOESCAPE;
    x |= GLOB_ABORTED | GLOB_NOSPACE | GLOB_NOMATCH | GLOB_NOSYS;
    (void)x;
}
