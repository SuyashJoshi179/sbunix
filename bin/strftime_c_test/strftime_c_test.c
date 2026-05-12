/* strftime_c_test (T2.21): the %c conversion specifier produces the
 * POSIX C-locale standard date+time representation
 * ("%a %b %e %H:%M:%S %Y") — the previous implementation fell through
 * to the default and emitted the literal "%c". */
#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

int main(void) {
    struct tm t = {
        .tm_sec  = 30,
        .tm_min  = 35,
        .tm_hour = 22,
        .tm_mday = 17,
        .tm_mon  = 0,         /* January */
        .tm_year = 110,       /* 1900 + 110 = 2010 */
        .tm_wday = 0,         /* Sunday */
        .tm_yday = 16,
        .tm_isdst = 0,
    };
    char buf[64];

    size_t n = strftime(buf, sizeof(buf), "%c", &t);
    /* Expected layout matches POSIX C-locale and matches ctime() minus
     * the trailing newline. */
    const char *want = "Sun Jan 17 22:35:30 2010";
    CHECK(n == strlen(want) && strcmp(buf, want) == 0, "POSIX %c layout");
    if (strcmp(buf, want) != 0)
        printf("  got: '%s'\n  want: '%s'\n", buf, want);

    /* Day-with-one-digit should be space-padded ("e", not "d"). */
    t.tm_mday = 3;
    n = strftime(buf, sizeof(buf), "%c", &t);
    const char *want2 = "Sun Jan  3 22:35:30 2010";
    CHECK(strcmp(buf, want2) == 0, "%c space-pads single-digit day");
    if (strcmp(buf, want2) != 0)
        printf("  got: '%s'\n  want: '%s'\n", buf, want2);

    /* %c next to a literal — the surrounding fmt must round-trip. */
    n = strftime(buf, sizeof(buf), "[%c]", &t);
    CHECK(buf[0] == '[' && buf[n - 1] == ']', "%c embeds inside literal");

    if (fails == 0) printf("strftime_c_test: PASS\n");
    else printf("strftime_c_test: %d FAIL(s)\n", fails);
    return fails;
}
