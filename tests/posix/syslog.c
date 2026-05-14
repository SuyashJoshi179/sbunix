/*
 * POSIX conformance test: <syslog.h>
 * Reference: docs/susv5-html/basedefs/syslog.h.html
 * Audited: openlog, closelog, syslog, setlogmask
 *
 * Required macros: LOG_EMERG..LOG_DEBUG, LOG_KERN..LOG_LOCAL0,
 *                  LOG_PID, LOG_CONS, LOG_NDELAY, LOG_ODELAY, LOG_NOWAIT
 *
 * Excluded (not implemented in our libc): openlog/closelog/syslog/setlogmask
 *   may not be present; pins commented if missing.
 */
#include <syslog.h>

#define PIN __attribute__((unused)) static

PIN void (*_pin_openlog)(const char *, int, int) = openlog;
PIN void (*_pin_closelog)(void) = closelog;
PIN void (*_pin_syslog)(int, const char *, ...) = syslog;
PIN int  (*_pin_setlogmask)(int) = setlogmask;

__attribute__((unused))
static void _macro_checks(void) {
    int x = LOG_EMERG | LOG_ALERT | LOG_CRIT | LOG_ERR
          | LOG_WARNING | LOG_NOTICE | LOG_INFO | LOG_DEBUG;
    x |= LOG_KERN | LOG_USER | LOG_MAIL | LOG_DAEMON | LOG_AUTH | LOG_SYSLOG;
    x |= LOG_PID | LOG_CONS | LOG_NDELAY | LOG_ODELAY | LOG_NOWAIT;
    (void)x;
}
