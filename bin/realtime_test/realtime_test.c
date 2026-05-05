/*
 * realtime_test — verify clock_gettime(CLOCK_REALTIME) and gettimeofday
 * are sourced from the Goldfish RTC, not from boot ticks.
 *
 * Symptom of the bug: clock_gettime(CLOCK_REALTIME) returned uptime
 * (small numbers like 2 or 5) instead of an epoch timestamp in the
 * billions, because syscall handlers were still using timer_ticks().
 *
 * Verifies:
 *   - CLOCK_REALTIME tv_sec is a real epoch value (post-2020)
 *   - gettimeofday tv_sec also post-2020
 *   - CLOCK_MONOTONIC stays uptime-based and is small (< 1 hour)
 *   - REALTIME and MONOTONIC differ by a large amount (epoch >> uptime)
 */
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EPOCH_FLOOR    1577836800L   /* 2020-01-01 UTC */
#define UPTIME_CEILING 3600L         /* assume tests start within 1 hour of boot */

static int pass = 0, fail = 0;
static void chk(int cond, const char *msg) {
    if (cond) { printf("[realtime_test] PASS  %s\n", msg); pass++; }
    else      { printf("[realtime_test] FAIL  %s\n", msg); fail++; }
}

int main(void) {
    struct timespec rt = {0, 0};
    int rc = clock_gettime(0 /* CLOCK_REALTIME */, &rt);
    chk(rc == 0, "clock_gettime(CLOCK_REALTIME) returns 0");
    chk(rt.tv_sec > EPOCH_FLOOR,
        "CLOCK_REALTIME tv_sec is post-2020 (RTC sourced, not uptime)");

    struct timespec mono = {0, 0};
    rc = clock_gettime(1 /* CLOCK_MONOTONIC */, &mono);
    chk(rc == 0, "clock_gettime(CLOCK_MONOTONIC) returns 0");
    chk(mono.tv_sec < UPTIME_CEILING,
        "CLOCK_MONOTONIC tv_sec is small (uptime, not epoch)");

    chk(rt.tv_sec > mono.tv_sec + EPOCH_FLOOR / 2,
        "REALTIME and MONOTONIC differ by >> uptime (separate clocks)");

    struct timeval tv = {0, 0};
    rc = gettimeofday(&tv, 0);
    chk(rc == 0, "gettimeofday returns 0");
    chk(tv.tv_sec > EPOCH_FLOOR,
        "gettimeofday tv_sec is post-2020 (RTC sourced, not uptime)");

    /* Sanity: REALTIME monotonic between two reads (RTC always advances). */
    struct timespec rt2 = {0, 0};
    sleep_ms(100);
    clock_gettime(0, &rt2);
    chk(rt2.tv_sec > rt.tv_sec ||
        (rt2.tv_sec == rt.tv_sec && rt2.tv_nsec >= rt.tv_nsec),
        "CLOCK_REALTIME is non-decreasing");

    if (fail == 0) printf("realtime_test: PASS (%d tests)\n", pass);
    else           printf("realtime_test: FAIL (%d/%d failed)\n",
                          fail, pass + fail);
    return fail ? 1 : 0;
}
