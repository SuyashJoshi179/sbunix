#pragma once
#include <sys/types.h>

typedef unsigned long rlim_t;

#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
#define RLIM_INFINITY  (~0UL)

struct rlimit {
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

static inline int getrlimit(int resource, struct rlimit *rlim) {
	(void)resource;
	rlim->rlim_cur = RLIM_INFINITY;
	rlim->rlim_max = RLIM_INFINITY;
	return 0;
}

static inline int setrlimit(int resource, const struct rlimit *rlim) {
	(void)resource; (void)rlim;
	return 0;
}
