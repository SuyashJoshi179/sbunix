#include <time.h>
#include <sys/times.h>
#include <stdint.h>
#include "syscall_priv.h"

static long ecall2(long num, long a0, long a1) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

int clock_gettime(int clockid, struct timespec *ts) {
    return (int)syscall_ret(ecall2(80, (long)clockid, (long)ts));
}

int gettimeofday(struct timeval *tv, void *tz) {
    return (int)syscall_ret(ecall2(81, (long)tv, (long)tz));
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)syscall_ret(ecall2(82, (long)req, (long)rem));
}

time_t time(time_t *tloc) {
    struct timespec ts;
    if (clock_gettime(0 /* CLOCK_REALTIME */, &ts) < 0) return (time_t)-1;
    if (tloc) *tloc = (time_t)ts.tv_sec;
    return (time_t)ts.tv_sec;
}

/*
 * times(2) — process-times stub.
 *
 * SBUnix does not yet track per-process user/system CPU ticks (no PCB
 * accounting), so all four tms_* fields are reported as 0. The return
 * value is monotonic elapsed ticks since boot, which is enough for code
 * that only cares about *differences* between two times() calls (the
 * common idiom for "how many ticks did this loop take?").
 *
 * If real CPU accounting is needed later, add a SYS_times syscall
 * backed by per-process tick counters incremented from the timer ISR.
 */
clock_t times(struct tms *buf) {
    if (buf) {
        buf->tms_utime  = 0;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    struct timespec ts;
    int rc = clock_gettime(1 /* CLOCK_MONOTONIC */, &ts);
    if (rc < 0) return (clock_t)-1;
    return (clock_t)(ts.tv_sec * CLOCKS_PER_SEC
                   + ts.tv_nsec * CLOCKS_PER_SEC / 1000000000L);
}
