#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Must match kernel/include/time.h */
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int    clock_gettime(int clockid, struct timespec *ts);
int    gettimeofday(struct timeval *tv, void *tz);
int    nanosleep(const struct timespec *req, struct timespec *rem);
time_t time(time_t *tloc);
