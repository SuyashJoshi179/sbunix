/*
 * POSIX conformance test: <sys/times.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_times.h.html
 *
 * Audited POSIX functions:
 *   - times
 *
 * Required POSIX struct: tms with members tms_utime, tms_stime, tms_cutime,
 * tms_cstime (all clock_t).
 */
#include <sys/times.h>

#define PIN __attribute__((unused)) static

PIN clock_t (*_pin_times)(struct tms *) = times;

__attribute__((unused))
static void _struct_fields(void) {
    struct tms t = {0};
    (void)t.tms_utime; (void)t.tms_stime;
    (void)t.tms_cutime; (void)t.tms_cstime;
}
