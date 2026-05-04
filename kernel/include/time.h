#pragma once
#include <stdint.h>

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

#define NSEC_PER_SEC    1000000000ULL
#define USEC_PER_SEC    1000000ULL
#define NSEC_PER_USEC   1000ULL
