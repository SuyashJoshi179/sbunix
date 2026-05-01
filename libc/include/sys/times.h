#pragma once
#include <sys/types.h>

/* Ticks-per-second for clock_t values returned by times(). Matches kernel
 * HZ (kernel/include/timer.h::TICKS_PER_SEC). */
#define CLOCKS_PER_SEC 100

struct tms {
    clock_t tms_utime;   /* this process: user CPU time used     */
    clock_t tms_stime;   /* this process: system CPU time used   */
    clock_t tms_cutime;  /* reaped children: user CPU time       */
    clock_t tms_cstime;  /* reaped children: system CPU time     */
};

clock_t times(struct tms *buf);
