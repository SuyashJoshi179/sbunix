/*
 * grader_time_test — Wall-clock epoch time (Eval 03).
 */
#include <stdio.h>
#include <time.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_time_test] PASS  %s\n", name);
    else { printf("[grader_time_test] FAIL  %s\n", name); fails++; }
}

int main(void) {
    printf("=== grader_time_test ===\n");

    time_t t1 = time(0);
    printf("  time() returned: %ld\n", (long)t1);
    check(t1 > 1000000000L, "time() > 1 billion (after 2001)");
    check(t1 < 3000000000L, "time() < 3 billion (sanity)");

    struct timeval tv;
    int rc = gettimeofday(&tv, 0);
    check(rc == 0, "gettimeofday() succeeds");
    long diff = (long)(tv.tv_sec - t1);
    if (diff < 0) diff = -diff;
    check(diff <= 2, "gettimeofday matches time() within 2s");

    struct timespec ts;
    rc = clock_gettime(CLOCK_REALTIME, &ts);
    check(rc == 0, "clock_gettime(REALTIME) succeeds");
    diff = (long)(ts.tv_sec - t1);
    if (diff < 0) diff = -diff;
    check(diff <= 2, "clock_gettime(REALTIME) matches time()");

    rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    check(rc == 0, "clock_gettime(MONOTONIC) succeeds");
    check(ts.tv_sec >= 0, "MONOTONIC tv_sec >= 0");

    time_t t2 = time(0);
    check(t2 >= t1, "second time() >= first");

    struct tm *tm = gmtime(&t1);
    check(tm != 0, "gmtime() non-NULL");
    if (tm) {
        int year = tm->tm_year + 1900;
        printf("  gmtime year: %d\n", year);
        check(year >= 2024 && year <= 2030, "gmtime year plausible");
    }

    printf("=== grader_time_test: %d failures ===\n", fails);
    return fails;
}
