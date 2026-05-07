#pragma once
#include <stdint.h>

/* POSIX getrlimit/setrlimit types. Single-user OS: setrlimit may raise
 * rlim_max without privilege check (documented deviation from POSIX). */

typedef uint64_t rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIM_INFINITY  ((rlim_t)-1)

/* Linux-compatible resource ids (sparse). Values match libc/include/sys/resource.h. */
#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NPROC    6
#define RLIMIT_NOFILE   7
#define RLIMIT_MEMLOCK  8
#define RLIMIT_AS       9

#define RLIMITS_NR      16
