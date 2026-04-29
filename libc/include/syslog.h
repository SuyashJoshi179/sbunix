#pragma once
#include <stdarg.h>

/* priorities */
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PRIMASK 0x07
#define LOG_PRI(p)        ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(f, p) (((f) << 3) | (p))
#define LOG_MASK(p)       (1 << (p))
#define LOG_UPTO(p)       ((1 << ((p)+1)) - 1)

/* facilities */
#define LOG_KERN     (0 << 3)
#define LOG_USER     (1 << 3)
#define LOG_MAIL     (2 << 3)
#define LOG_DAEMON   (3 << 3)
#define LOG_AUTH     (4 << 3)
#define LOG_SYSLOG   (5 << 3)
#define LOG_LPR      (6 << 3)
#define LOG_NEWS     (7 << 3)
#define LOG_UUCP     (8 << 3)
#define LOG_CRON     (9 << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_LOCAL0   (16 << 3)

/* openlog flags */
#define LOG_PID    0x01
#define LOG_CONS   0x02
#define LOG_ODELAY 0x04
#define LOG_NDELAY 0x08
#define LOG_NOWAIT 0x10
#define LOG_PERROR 0x20

void openlog(const char *ident, int option, int facility);
void closelog(void);
int  setlogmask(int mask);
void syslog(int priority, const char *fmt, ...);
void vsyslog(int priority, const char *fmt, va_list ap);
