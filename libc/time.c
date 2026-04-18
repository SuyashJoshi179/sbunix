#include <time.h>
#include <stdint.h>

static long ecall2(long num, long a0, long a1) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

int clock_gettime(int clockid, struct timespec *ts) {
    return (int)ecall2(80, (long)clockid, (long)ts);
}

int gettimeofday(struct timeval *tv, void *tz) {
    return (int)ecall2(81, (long)tv, (long)tz);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)ecall2(82, (long)req, (long)rem);
}

time_t time(time_t *tloc) {
    struct timespec ts;
    if (clock_gettime(1 /* CLOCK_MONOTONIC */, &ts) < 0) return (time_t)-1;
    if (tloc) *tloc = (time_t)ts.tv_sec;
    return (time_t)ts.tv_sec;
}
