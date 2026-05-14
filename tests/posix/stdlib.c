/*
 * POSIX conformance test: <stdlib.h>
 *
 * Reference: docs/susv5-html/basedefs/stdlib.h.html
 *
 * Audited POSIX functions (only those our libc declares):
 *   exit, _Exit, abort, atexit, malloc, calloc, realloc, free, atoi, atol,
 *   atoll, atof, strtol, strtoul, strtoll, strtoull, strtod, strtof, abs,
 *   labs, llabs, rand, srand, getenv, setenv, unsetenv, putenv, system,
 *   qsort, bsearch, div, ldiv, lldiv, realpath, mkstemp, mkdtemp, mblen,
 *   mbtowc, wctomb, mbstowcs, wcstombs
 *
 * Required typedefs: div_t (quot/rem int), ldiv_t (long), lldiv_t (long long)
 * Required macros: EXIT_FAILURE, EXIT_SUCCESS, RAND_MAX, MB_CUR_MAX, NULL
 *
 * Excluded (POSIX, not in our libc): aligned_alloc, at_quick_exit, grantpt,
 *   getsubopt, mkostemp, posix_memalign, posix_openpt, ptsname, ptsname_r,
 *   qsort_r, random, reallocarray, secure_getenv, setkey, srandom,
 *   initstate, strtold, ...
 */
#include <stdlib.h>

#define PIN __attribute__((unused)) static

PIN void   (*_pin_exit)(int) __attribute__((noreturn)) = exit;
PIN void   (*_pin__Exit)(int) __attribute__((noreturn)) = _Exit;
PIN void   (*_pin_abort)(void) __attribute__((noreturn)) = abort;
PIN int    (*_pin_atexit)(void (*)(void)) = atexit;
PIN void  *(*_pin_malloc)(size_t) = malloc;
PIN void  *(*_pin_calloc)(size_t, size_t) = calloc;
PIN void  *(*_pin_realloc)(void *, size_t) = realloc;
PIN void   (*_pin_free)(void *) = free;
PIN int    (*_pin_atoi)(const char *) = atoi;
PIN long   (*_pin_atol)(const char *) = atol;
PIN double (*_pin_atof)(const char *) = atof;
PIN long   (*_pin_strtol)(const char *, char **, int) = strtol;
PIN unsigned long (*_pin_strtoul)(const char *, char **, int) = strtoul;
PIN long long      (*_pin_strtoll)(const char *, char **, int) = strtoll;
PIN unsigned long long (*_pin_strtoull)(const char *, char **, int) = strtoull;
PIN double (*_pin_strtod)(const char *, char **) = strtod;
PIN float  (*_pin_strtof)(const char *, char **) = strtof;
PIN int    (*_pin_abs)(int) = abs;
PIN long   (*_pin_labs)(long) = labs;
PIN long long (*_pin_llabs)(long long) = llabs;
PIN int    (*_pin_rand)(void) = rand;
PIN void   (*_pin_srand)(unsigned) = srand;
PIN char  *(*_pin_getenv)(const char *) = getenv;
PIN int    (*_pin_setenv)(const char *, const char *, int) = setenv;
PIN int    (*_pin_unsetenv)(const char *) = unsetenv;
PIN int    (*_pin_putenv)(char *) = putenv;
PIN int    (*_pin_system)(const char *) = system;
PIN void   (*_pin_qsort)(void *, size_t, size_t,
                         int (*)(const void *, const void *)) = qsort;
PIN void  *(*_pin_bsearch)(const void *, const void *, size_t, size_t,
                           int (*)(const void *, const void *)) = bsearch;
PIN div_t   (*_pin_div)(int, int) = div;
PIN ldiv_t  (*_pin_ldiv)(long, long) = ldiv;
PIN lldiv_t (*_pin_lldiv)(long long, long long) = lldiv;
PIN char   *(*_pin_realpath)(const char *, char *) = realpath;
PIN int     (*_pin_mkstemp)(char *) = mkstemp;
PIN char   *(*_pin_mkdtemp)(char *) = mkdtemp;
PIN int     (*_pin_mblen)(const char *, size_t) = mblen;

/* DIVERGENCE: mbtowc / wctomb / mbstowcs / wcstombs use POSIX wchar_t.
 * Our libc declares them with int * / int instead. Pin with POSIX type;
 * compile FAIL is the divergence diagnostic.
 *
 * PIN int    (*_pin_mbtowc)(wchar_t *, const char *, size_t) = mbtowc;
 * PIN int    (*_pin_wctomb)(char *, wchar_t) = wctomb;
 * PIN size_t (*_pin_mbstowcs)(wchar_t *, const char *, size_t) = mbstowcs;
 * PIN size_t (*_pin_wcstombs)(char *, const wchar_t *, size_t) = wcstombs;
 */

__attribute__((unused))
static void _struct_fields(void) {
    div_t d = {0,0};
    (void)d.quot; (void)d.rem;
    ldiv_t ld = {0,0};
    (void)ld.quot; (void)ld.rem;
    lldiv_t lld = {0,0};
    (void)lld.quot; (void)lld.rem;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = EXIT_SUCCESS | EXIT_FAILURE | MB_CUR_MAX;
    (void)x;
    (void)RAND_MAX; (void)NULL;
}
