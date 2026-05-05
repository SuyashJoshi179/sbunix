#include <stdio.h>
#include <time.h>

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else       { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    struct timespec t1, t2;

    /* clock_gettime returns 0 */
    int r = clock_gettime(CLOCK_MONOTONIC, &t1);
    check(r == 0, "clock_gettime MONOTONIC returns 0");

    /* tv_nsec in [0, 1e9) */
    check(t1.tv_nsec >= 0 && t1.tv_nsec < 1000000000LL,
          "tv_nsec in [0, 1e9)");

    /* CLOCK_REALTIME also works */
    r = clock_gettime(CLOCK_REALTIME, &t2);
    check(r == 0, "clock_gettime REALTIME returns 0");

    /* CLOCK_REALTIME must be wall-clock (Goldfish RTC), not boot-relative
     * uptime. Threshold 2024-01-01 UTC = 1704067200; bound 2100-01-01 =
     * 4102444800. A regression to uptime/epoch-0 fails the lower bound. */
    check(t2.tv_sec > 1704067200LL,
          "CLOCK_REALTIME tv_sec past 2024-01-01 (RTC, not uptime)");
    check(t2.tv_sec < 4102444800LL,
          "CLOCK_REALTIME tv_sec below 2100 sanity bound");
    check(t2.tv_nsec >= 0 && t2.tv_nsec < 1000000000LL,
          "CLOCK_REALTIME tv_nsec in [0, 1e9)");

    /* REALTIME and MONOTONIC must diverge: MONOTONIC is uptime (small),
     * REALTIME is wall clock (huge). If they're close, REALTIME regressed. */
    struct timespec mono;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    check(t2.tv_sec - mono.tv_sec > 1000000000LL,
          "REALTIME and MONOTONIC differ by >1e9 sec (RTC vs uptime)");

    /* Bad clockid returns error */
    r = clock_gettime(99, &t1);
    check(r < 0, "clock_gettime bad clockid returns error");

    /* Monotonic: second call >= first */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    check(t2.tv_sec > t1.tv_sec ||
          (t2.tv_sec == t1.tv_sec && t2.tv_nsec >= t1.tv_nsec),
          "monotonic clock non-decreasing");

    /* nanosleep 20 ms; time advances */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    struct timespec req = { .tv_sec = 0, .tv_nsec = 20000000 /* 20 ms */ };
    nanosleep(&req, 0);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    long delta_ms = (long)((t2.tv_sec - t1.tv_sec) * 1000LL +
                            (t2.tv_nsec - t1.tv_nsec) / 1000000LL);
    check(delta_ms >= 10, "nanosleep 20ms: elapsed >= 10ms");

    /* nanosleep invalid tv_nsec */
    req.tv_sec  = 0;
    req.tv_nsec = 1000000000LL;
    r = nanosleep(&req, 0);
    check(r < 0, "nanosleep invalid tv_nsec returns error");

    /* gettimeofday */
    struct timeval tv;
    r = gettimeofday(&tv, 0);
    check(r == 0, "gettimeofday returns 0");
    check(tv.tv_usec >= 0 && tv.tv_usec < 1000000LL,
          "gettimeofday tv_usec in [0, 1e6)");
    /* gettimeofday must be wall clock from RTC, not boot-relative uptime. */
    check(tv.tv_sec > 1704067200LL,
          "gettimeofday tv_sec past 2024-01-01 (RTC, not uptime)");
    check(tv.tv_sec < 4102444800LL,
          "gettimeofday tv_sec below 2100 sanity bound");

    printf("time_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
