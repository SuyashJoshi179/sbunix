#include <sys/resource.h>
#include <string.h>
#include <errno.h>

/* No kernel rlimit syscalls. We service get/set in userspace by remembering
 * what was last set; nothing actually enforces the limits. getrusage
 * returns zeroed counters. */

static struct rlimit limits[RLIM_NLIMITS] = {
    [RLIMIT_CPU]     = { RLIM_INFINITY, RLIM_INFINITY },
    [RLIMIT_FSIZE]   = { RLIM_INFINITY, RLIM_INFINITY },
    [RLIMIT_DATA]    = { RLIM_INFINITY, RLIM_INFINITY },
    [RLIMIT_STACK]   = { 0x100000,      0x100000 },     /* 1 MiB user stack */
    [RLIMIT_CORE]    = { 0,             0 },            /* no core dumps */
    [RLIMIT_RSS]     = { RLIM_INFINITY, RLIM_INFINITY },
    [RLIMIT_NPROC]   = { 64,            64 },
    [RLIMIT_NOFILE]  = { 32,            32 },
    [RLIMIT_MEMLOCK] = { RLIM_INFINITY, RLIM_INFINITY },
    [RLIMIT_AS]      = { RLIM_INFINITY, RLIM_INFINITY },
};

int getrlimit(int resource, struct rlimit *rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS) { errno = EINVAL; return -1; }
    if (!rlim) { errno = EFAULT; return -1; }
    *rlim = limits[resource];
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS) { errno = EINVAL; return -1; }
    if (!rlim) { errno = EFAULT; return -1; }
    if (rlim->rlim_cur > rlim->rlim_max) { errno = EINVAL; return -1; }
    limits[resource] = *rlim;
    return 0;
}

int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) { errno = EFAULT; return -1; }
    memset(usage, 0, sizeof(*usage));
    return 0;
}

int getpriority(int which, id_t who) {
    (void)which; (void)who;
    return 0;
}

int setpriority(int which, id_t who, int prio) {
    (void)which; (void)who; (void)prio;
    return 0;
}
