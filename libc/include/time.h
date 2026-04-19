#pragma once
#include <stdint.h>
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

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

int    clock_gettime(int clockid, struct timespec *ts);
int    gettimeofday(struct timeval *tv, void *tz);
int    nanosleep(const struct timespec *req, struct timespec *rem);
time_t time(time_t *tloc);
