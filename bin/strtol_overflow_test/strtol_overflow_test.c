/* strtol_overflow_test (T2.16): POSIX overflow semantics for the strto*
 * family. On overflow strtol returns LONG_MAX or LONG_MIN (matching the
 * sign of the value), strtoul returns ULONG_MAX, and errno is set to
 * ERANGE. endp must still advance past every digit consumed. */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

int main(void) {
    char *endp;
    long v;
    unsigned long u;
    long long ll;
    unsigned long long ull;

    errno = 0;
    v = strtol("42", &endp, 10);
    CHECK(v == 42 && errno == 0 && *endp == '\0', "baseline 42");

    errno = 0;
    v = strtol("9223372036854775807", &endp, 10);
    CHECK(v == LONG_MAX && errno == 0, "LONG_MAX exact");

    errno = 0;
    v = strtol("9223372036854775808", &endp, 10);
    CHECK(v == LONG_MAX && errno == ERANGE, "+overflow by 1");

    errno = 0;
    v = strtol("99999999999999999999999", &endp, 10);
    CHECK(v == LONG_MAX && errno == ERANGE, "large +overflow");

    errno = 0;
    v = strtol("-9223372036854775808", &endp, 10);
    CHECK(v == LONG_MIN && errno == 0, "LONG_MIN exact");

    errno = 0;
    v = strtol("-9223372036854775809", &endp, 10);
    CHECK(v == LONG_MIN && errno == ERANGE, "-overflow by 1");

    errno = 0;
    u = strtoul("18446744073709551615", &endp, 10);
    CHECK(u == ULONG_MAX && errno == 0, "ULONG_MAX exact");

    errno = 0;
    u = strtoul("18446744073709551616", &endp, 10);
    CHECK(u == ULONG_MAX && errno == ERANGE, "ULONG_MAX +overflow");

    errno = 0;
    v = strtol("0x7fffffffffffffff", &endp, 0);
    CHECK(v == LONG_MAX && errno == 0, "hex LONG_MAX exact");

    errno = 0;
    v = strtol("0x8000000000000000", &endp, 0);
    CHECK(v == LONG_MAX && errno == ERANGE, "hex +overflow");

    errno = 0;
    v = strtol("99999999999999999999tail", &endp, 10);
    CHECK(v == LONG_MAX && errno == ERANGE && strcmp(endp, "tail") == 0,
          "endp advances past overflow digits");

    errno = 42;
    v = strtol("zzz", &endp, 10);
    CHECK(v == 0 && errno == 42, "no conv preserves errno");

    errno = 0;
    ll = strtoll("99999999999999999999", &endp, 10);
    CHECK(ll == LLONG_MAX && errno == ERANGE, "strtoll inherits ERANGE");

    errno = 0;
    ull = strtoull("18446744073709551616", &endp, 10);
    CHECK(ull == ULLONG_MAX && errno == ERANGE, "strtoull inherits ERANGE");

    /* strtoul('-'): POSIX says a leading '-' negates the result mod 2^N.
     * strtoul("-1") must return ULONG_MAX (not 0) with errno unchanged
     * and endp at the terminator. */
    errno = 0;
    u = strtoul("-1", &endp, 10);
    CHECK(u == ULONG_MAX && errno == 0 && *endp == '\0',
          "strtoul '-1' returns ULONG_MAX");

    errno = 0;
    u = strtoul("-2", &endp, 10);
    CHECK(u == ULONG_MAX - 1 && errno == 0 && *endp == '\0',
          "strtoul '-2' returns ULONG_MAX-1");

    /* 0x prefix without a hex digit: glibc treats the leading '0' as a
     * one-digit (octal/hex 0) conversion and leaves the 'x' for endp.
     * Pre-fix our code consumed "0x" unconditionally and returned no
     * conversion (endp == s). */
    errno = 0;
    v = strtol("0xz", &endp, 0);
    CHECK(v == 0 && errno == 0 && *endp == 'x',
          "strtol '0xz' base=0: endp at 'x'");

    errno = 0;
    v = strtol("0x", &endp, 0);
    CHECK(v == 0 && errno == 0 && *endp == 'x',
          "strtol '0x' base=0: endp at 'x'");

    errno = 0;
    u = strtoul("0xz", &endp, 16);
    CHECK(u == 0 && errno == 0 && *endp == 'x',
          "strtoul '0xz' base=16: endp at 'x'");

    if (fails == 0) printf("strtol_overflow_test: PASS\n");
    else printf("strtol_overflow_test: %d FAIL(s)\n", fails);
    return fails;
}
