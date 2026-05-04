#include <stdio.h>
#include <time.h>
#include <sys/time.h>

int main(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        printf("wallclock_test: clock_gettime FAIL\n");
        return 1;
    }

    if (ts.tv_sec < 1700000000LL) {
        printf("wallclock_test: clock_gettime tv_sec=%lld too small (want > 1.7e9)\n",
               (long long)ts.tv_sec);
        return 1;
    }
    if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) {
        printf("wallclock_test: clock_gettime tv_nsec=%lld out of range\n",
               (long long)ts.tv_nsec);
        return 1;
    }

    struct timeval tv;
    if (gettimeofday(&tv, 0) < 0) {
        printf("wallclock_test: gettimeofday FAIL\n");
        return 1;
    }
    if (tv.tv_sec < 1700000000LL) {
        printf("wallclock_test: gettimeofday tv_sec=%lld too small\n",
               (long long)tv.tv_sec);
        return 1;
    }
    if (tv.tv_usec < 0 || tv.tv_usec >= 1000000LL) {
        printf("wallclock_test: gettimeofday tv_usec=%lld out of range\n",
               (long long)tv.tv_usec);
        return 1;
    }

    /* MONOTONIC stays on tick clock — small value after boot is normal. */
    struct timespec mts;
    if (clock_gettime(CLOCK_MONOTONIC, &mts) < 0) {
        printf("wallclock_test: clock_gettime MONOTONIC FAIL\n");
        return 1;
    }
    if (mts.tv_sec > 1000000LL) {
        printf("wallclock_test: MONOTONIC tv_sec=%lld unexpectedly large\n",
               (long long)mts.tv_sec);
        return 1;
    }

    printf("wallclock_test: PASS realtime=%lld monotonic=%lld\n",
           (long long)ts.tv_sec, (long long)mts.tv_sec);
    return 0;
}
