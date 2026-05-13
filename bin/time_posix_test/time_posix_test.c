#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("time_posix_test: FAIL %s\n", msg); fails++; } \
} while (0)

int main(void) {
    /* Known epoch: 2024-01-15 13:30:45 UTC = 1705325445. */
    time_t t = 1705325445;

    struct tm tm;
    CHECK(gmtime_r(&t, &tm) == &tm, "gmtime_r returns out");
    CHECK(tm.tm_year == 124,        "tm_year == 2024-1900");
    CHECK(tm.tm_mon  == 0,          "tm_mon  == 0 (January)");
    CHECK(tm.tm_mday == 15,         "tm_mday == 15");
    CHECK(tm.tm_hour == 13,         "tm_hour == 13");
    CHECK(tm.tm_min  == 30,         "tm_min  == 30");
    CHECK(tm.tm_sec  == 45,         "tm_sec  == 45");
    CHECK(tm.tm_wday == 1,          "tm_wday == 1 (Monday)");
    CHECK(tm.tm_yday == 14,         "tm_yday == 14");

    /* mktime/timegm round-trip. */
    time_t back = timegm(&tm);
    CHECK(back == t, "timegm round-trip");

    /* Leap year: 2024-02-29 valid. */
    struct tm leap = {0};
    leap.tm_year = 124; leap.tm_mon = 1; leap.tm_mday = 29;
    time_t leap_t = mktime(&leap);
    CHECK(leap_t > 0,         "mktime accepts 2024-02-29");
    CHECK(leap.tm_yday == 59, "leap-day yday normalised to 59");

    /* asctime format: "Mon Jan 15 13:30:45 2024\n". */
    char abuf[32];
    char *as = asctime_r(&tm, abuf);
    CHECK(strcmp(as, "Mon Jan 15 13:30:45 2024\n") == 0, "asctime_r format");

    /* ctime is asctime composed with gmtime. */
    char cbuf[32];
    char *ct = ctime_r(&t, cbuf);
    CHECK(strcmp(ct, "Mon Jan 15 13:30:45 2024\n") == 0, "ctime_r format");

    /* strftime: %F %T %Y %p %A %B */
    char sbuf[64];
    size_t n = strftime(sbuf, sizeof(sbuf), "%F %T", &tm);
    CHECK(n == 19, "strftime %F %T length");
    CHECK(strcmp(sbuf, "2024-01-15 13:30:45") == 0, "strftime %F %T value");

    n = strftime(sbuf, sizeof(sbuf), "%A %B %d, %Y", &tm);
    CHECK(strcmp(sbuf, "Monday January 15, 2024") == 0, "strftime long-name");

    n = strftime(sbuf, sizeof(sbuf), "%I:%M:%S %p", &tm);
    CHECK(strcmp(sbuf, "01:30:45 PM") == 0, "strftime 12-hour clock");

    /* Truncation: short buffer, ensure NUL terminator placed. */
    char tiny[6];
    n = strftime(tiny, sizeof(tiny), "%Y-%m-%d", &tm);
    CHECK(tiny[5] == '\0', "strftime nul-terminates short buffer");

    /* tzset/timezone exposed and harmless. */
    tzset();
    CHECK(timezone == 0,                 "timezone == 0 (UTC)");
    CHECK(strcmp(tzname[0], "UTC") == 0, "tzname[0] == UTC");

    /* clock() returns ticks since boot — small positive. */
    clock_t c = clock();
    CHECK(c >= 0, "clock() non-negative");

    /* clock_getres: REALTIME = 1 ns, MONOTONIC = 10 ms (HZ=100). */
    struct timespec res = {-1, -1};
    CHECK(clock_getres(0 /* CLOCK_REALTIME */, &res) == 0, "clock_getres REALTIME ok");
    CHECK(res.tv_sec == 0 && res.tv_nsec == 1,            "clock_getres REALTIME == 1ns");
    res.tv_sec = -1; res.tv_nsec = -1;
    CHECK(clock_getres(1 /* CLOCK_MONOTONIC */, &res) == 0, "clock_getres MONOTONIC ok");
    CHECK(res.tv_sec == 0 && res.tv_nsec > 0,               "clock_getres MONOTONIC positive");
    CHECK(clock_getres(0, NULL) == 0,                       "clock_getres NULL res allowed");
    CHECK(clock_getres(99, NULL) == -1 && errno == EINVAL,  "clock_getres bad id -> EINVAL");

    /* clock_settime: CLOCK_MONOTONIC rejected. */
    struct timespec bogus = { 0, 0 };
    CHECK(clock_settime(1, &bogus) == -1 && errno == EINVAL, "clock_settime MONOTONIC -> EINVAL");

    /* Round-trip: read REALTIME, set 100 years forward, observe the jump,
     * restore the original time. Bounds rather than equality to tolerate
     * RTC advancing between calls. */
    struct timespec orig, after;
    CHECK(clock_gettime(0, &orig) == 0, "clock_gettime before set");
    struct timespec future = { orig.tv_sec + 100LL * 365 * 86400, 0 };
    CHECK(clock_settime(0, &future) == 0,           "clock_settime forward");
    CHECK(clock_gettime(0, &after)  == 0,           "clock_gettime after set");
    CHECK(after.tv_sec >= future.tv_sec,            "REALTIME advanced past target");
    CHECK(after.tv_sec  < future.tv_sec + 10,       "REALTIME within 10s of target");
    /* Restore — leaves the clock close to wall time for later tests. */
    CHECK(clock_settime(0, &orig) == 0, "clock_settime restore");

    if (fails == 0) {
        printf("time_posix_test: PASS\n");
        return 0;
    }
    printf("time_posix_test: %d FAIL\n", fails);
    return 1;
}
