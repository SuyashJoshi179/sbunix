/*
 * POSIX conformance test: <sys/time.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_time.h.html
 *
 * SUSv5 reduces this header to:
 *   - utimes() (marked LEGACY)
 *   - the timespec/timeval struct types reachable via <time.h>
 *   - select() and FD_* moved to <sys/select.h>
 *
 * Earlier POSIX editions (2017 and prior) placed gettimeofday/getitimer/
 * setitimer here. Our libc's sys/time.h is a one-line shim that includes
 * <time.h>; gettimeofday is declared in <time.h> and reachable via this
 * header transitively. We pin it here so user code that includes only
 * <sys/time.h> (the POSIX-2017 canonical location) gets a working
 * conformance check.
 *
 * Audited POSIX functions:
 *   - gettimeofday (POSIX-2017; SUSv5 deprecated but still in our libc)
 *
 * Excluded (POSIX functions not yet implemented):
 *   - utimes
 *   - getitimer / setitimer
 *
 * Note: struct timeval, struct timespec, struct timezone are exposed
 * transitively via the <time.h> include and audited in tests/posix/time.c.
 */
#include <sys/time.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_gettimeofday)(struct timeval *, struct timezone *) = gettimeofday;

/* Struct-field references for the timeval-family types reachable from
 * this header (transitively via <time.h>). */
__attribute__((unused))
static void _struct_fields(void) {
    struct timeval tv = {0};
    (void)tv.tv_sec; (void)tv.tv_usec;

    struct timezone tz = {0};
    (void)tz.tz_minuteswest; (void)tz.tz_dsttime;
}
