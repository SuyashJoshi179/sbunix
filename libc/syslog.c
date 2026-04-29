#include <syslog.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

/* No syslogd in SBUnix. We forward each call to stderr (fd 2) — close
 * enough for ports that just want diagnostic output without crashing on
 * the missing socket/dgram path. */

static const char *log_ident   = 0;
static int         log_options = 0;
static int         log_mask    = 0xFF;

void openlog(const char *ident, int option, int facility) {
    (void)facility;
    log_ident   = ident;
    log_options = option;
}

void closelog(void) {
    log_ident   = 0;
    log_options = 0;
}

int setlogmask(int mask) {
    int old = log_mask;
    if (mask) log_mask = mask;
    return old;
}

static const char *prio_label(int pri) {
    switch (LOG_PRI(pri)) {
    case LOG_EMERG:   return "emerg";
    case LOG_ALERT:   return "alert";
    case LOG_CRIT:    return "crit";
    case LOG_ERR:     return "err";
    case LOG_WARNING: return "warn";
    case LOG_NOTICE:  return "notice";
    case LOG_INFO:    return "info";
    default:          return "debug";
    }
}

void vsyslog(int priority, const char *fmt, va_list ap) {
    if (!(log_mask & LOG_MASK(LOG_PRI(priority)))) return;
    if (log_ident) fprintf(stderr, "%s", log_ident);
    if (log_options & LOG_PID) fprintf(stderr, "[%d]", getpid());
    if (log_ident || (log_options & LOG_PID)) fprintf(stderr, ": ");
    fprintf(stderr, "<%s> ", prio_label(priority));
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void syslog(int priority, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}
