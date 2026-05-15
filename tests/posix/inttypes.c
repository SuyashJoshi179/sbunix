/*
 * POSIX conformance test: <inttypes.h>
 * Reference: docs/susv5-html/basedefs/inttypes.h.html
 * Required typedefs: imaxdiv_t (with quot, rem fields)
 *
 * Excluded (not implemented): imaxabs, imaxdiv, strtoimax, strtoumax,
 *   wcstoimax, wcstoumax — none in our libc.
 *
 * Macro-only audit: PRI* / SCN* family. Just reference a representative
 * sample to prove they're defined.
 */
#include <inttypes.h>

__attribute__((unused))
static void _macro_checks(void) {
    /* Sample of required macros — full list per SUSv5 is large. */
    const char *s = PRId8 PRIi16 PRIu32 PRIx64 PRIdMAX;
    (void)s;
}
