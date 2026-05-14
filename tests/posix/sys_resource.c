/*
 * POSIX conformance test: <sys/resource.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_resource.h.html
 *
 * Audited POSIX functions: getrlimit, setrlimit, getrusage, getpriority,
 *                          setpriority
 * Required struct rlimit: rlim_cur, rlim_max (rlim_t)
 * Required struct rusage: at minimum ru_utime, ru_stime (struct timeval)
 * Required macros: RLIMIT_CORE, RLIMIT_CPU, RLIMIT_DATA, RLIMIT_FSIZE,
 *                  RLIMIT_NOFILE, RLIMIT_STACK, RLIMIT_AS, RLIM_INFINITY,
 *                  RLIM_SAVED_CUR, RLIM_SAVED_MAX,
 *                  RUSAGE_SELF, RUSAGE_CHILDREN,
 *                  PRIO_PROCESS, PRIO_PGRP, PRIO_USER
 *
 * Non-POSIX in our libc: RLIMIT_RSS, RLIMIT_NPROC, RLIMIT_MEMLOCK,
 *                       RLIM_NLIMITS (BSD extensions); ru_* fields beyond
 *                       ru_utime/ru_stime are non-POSIX.
 */
#include <sys/resource.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_getrlimit)(int, struct rlimit *) = getrlimit;
PIN int (*_pin_setrlimit)(int, const struct rlimit *) = setrlimit;
PIN int (*_pin_getrusage)(int, struct rusage *) = getrusage;
PIN int (*_pin_getpriority)(int, id_t) = getpriority;
PIN int (*_pin_setpriority)(int, id_t, int) = setpriority;

__attribute__((unused))
static void _struct_fields(void) {
    struct rlimit r;
    __builtin_memset(&r, 0, sizeof r);
    (void)r.rlim_cur; (void)r.rlim_max;

    struct rusage u;
    __builtin_memset(&u, 0, sizeof u);
    (void)u.ru_utime; (void)u.ru_stime;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    x |= RLIMIT_CORE; x |= RLIMIT_CPU; x |= RLIMIT_DATA; x |= RLIMIT_FSIZE;
    x |= RLIMIT_NOFILE; x |= RLIMIT_STACK; x |= RLIMIT_AS;
    x |= RUSAGE_SELF; x |= RUSAGE_CHILDREN;
    x |= PRIO_PROCESS; x |= PRIO_PGRP; x |= PRIO_USER;
    (void)x;
    (void)RLIM_INFINITY; (void)RLIM_SAVED_CUR; (void)RLIM_SAVED_MAX;
}
