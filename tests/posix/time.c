/*
 * POSIX conformance test: <time.h>
 *
 * Reference: docs/susv5-html/basedefs/time.h.html
 *
 * Audited POSIX functions:
 *   - clock_gettime
 *   - clock_settime
 *   - clock_getres
 *   - nanosleep
 *   - time
 *   - clock
 *   - difftime
 *   - gmtime
 *   - gmtime_r
 *   - localtime
 *   - localtime_r
 *   - mktime
 *   - asctime
 *   - asctime_r
 *   - ctime
 *   - ctime_r
 *   - strftime
 *   - tzset
 *
 * Excluded (non-POSIX functions our libc declares here):
 *   - timegm — non-standard (GNU/BSD); not in SUSv5
 *   - gettimeofday — POSIX assigns it to <sys/time.h>; audited there
 *
 * Excluded (POSIX functions not yet implemented, audit when added):
 *   - getdate
 *   - clock_getcpuclockid
 *   - clock_nanosleep
 *   - strptime
 *   - timer_create / timer_delete / timer_settime / timer_gettime / timer_getoverrun
 *   - timespec_get
 */
#include <time.h>

#define PIN __attribute__((unused)) static

/* Prototype pins: LHS types come verbatim from
 * docs/susv5-html/basedefs/time.h.html. */
PIN int        (*_pin_clock_gettime)(clockid_t, struct timespec *) = clock_gettime;
PIN int        (*_pin_clock_settime)(clockid_t, const struct timespec *) = clock_settime;
PIN int        (*_pin_clock_getres)(clockid_t, struct timespec *) = clock_getres;
PIN int        (*_pin_nanosleep)(const struct timespec *, struct timespec *) = nanosleep;
PIN time_t     (*_pin_time)(time_t *) = time;
PIN clock_t    (*_pin_clock)(void) = clock;
PIN double     (*_pin_difftime)(time_t, time_t) = difftime;
PIN struct tm *(*_pin_gmtime)(const time_t *) = gmtime;
PIN struct tm *(*_pin_gmtime_r)(const time_t *, struct tm *) = gmtime_r;
PIN struct tm *(*_pin_localtime)(const time_t *) = localtime;
PIN struct tm *(*_pin_localtime_r)(const time_t *, struct tm *) = localtime_r;
PIN time_t     (*_pin_mktime)(struct tm *) = mktime;
PIN char      *(*_pin_asctime)(const struct tm *) = asctime;
PIN char      *(*_pin_asctime_r)(const struct tm *, char *) = asctime_r;
PIN char      *(*_pin_ctime)(const time_t *) = ctime;
PIN char      *(*_pin_ctime_r)(const time_t *, char *) = ctime_r;
PIN size_t     (*_pin_strftime)(char *, size_t, const char *, const struct tm *) = strftime;
PIN void       (*_pin_tzset)(void) = tzset;

/* Struct-field references — POSIX-required members of struct tm and
 * struct timespec must be visible from <time.h>. */
__attribute__((unused))
static void _struct_fields(void) {
    struct tm t = {0};
    (void)t.tm_sec; (void)t.tm_min; (void)t.tm_hour;
    (void)t.tm_mday; (void)t.tm_mon; (void)t.tm_year;
    (void)t.tm_wday; (void)t.tm_yday; (void)t.tm_isdst;

    struct timespec ts = {0};
    (void)ts.tv_sec; (void)ts.tv_nsec;
}

/* External-variable pins — POSIX names that <time.h> must expose. */
PIN char **_pin_tzname    = &tzname[0];
PIN long  *_pin_timezone  = &timezone;
PIN int   *_pin_daylight  = &daylight;
