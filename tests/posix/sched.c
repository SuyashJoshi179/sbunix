/*
 * POSIX conformance test: <sched.h>
 *
 * Reference: docs/susv5-html/basedefs/sched.h.html
 *
 * Audited: sched_yield, sched_get_priority_max, sched_get_priority_min,
 *          sched_setscheduler, sched_getscheduler
 * Excluded (not implemented): sched_getparam, sched_setparam, sched_rr_get_interval
 * Excluded (non-POSIX): SCHED_BATCH, SCHED_IDLE (Linux extensions)
 */
#include <sched.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_sched_yield)(void) = sched_yield;
PIN int (*_pin_sched_get_priority_max)(int) = sched_get_priority_max;
PIN int (*_pin_sched_get_priority_min)(int) = sched_get_priority_min;
PIN int (*_pin_sched_setscheduler)(pid_t, int, const struct sched_param *) = sched_setscheduler;
PIN int (*_pin_sched_getscheduler)(pid_t) = sched_getscheduler;

__attribute__((unused))
static void _struct_fields(void) {
    struct sched_param p = {0};
    (void)p.sched_priority;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = SCHED_OTHER | SCHED_FIFO | SCHED_RR;
    (void)x;
}
