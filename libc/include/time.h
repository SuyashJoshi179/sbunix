#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Must match kernel/include/time.h */
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timezone {
    int tz_minuteswest; /* minutes west of Greenwich */
    int tz_dsttime;     /* type of DST correction */
};

struct tm {
    int tm_sec;     /* 0-60 (leap second) */
    int tm_min;     /* 0-59 */
    int tm_hour;    /* 0-23 */
    int tm_mday;    /* 1-31 */
    int tm_mon;     /* 0-11 (Jan=0) */
    int tm_year;    /* years since 1900 */
    int tm_wday;    /* 0-6 (Sun=0) */
    int tm_yday;    /* 0-365 */
    int tm_isdst;   /* always 0 (no TZ db) */
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

/* utimensat(2): tv_nsec sentinels meaning "use current time" and "leave
 * this timestamp unchanged". Values match Linux's <sys/stat.h>. */
#define UTIME_NOW    ((1L << 30) - 1L)
#define UTIME_OMIT   ((1L << 30) - 2L)

/* clock() unit; matches sys/times.h for the same reason: kernel HZ. */
#define CLOCKS_PER_SEC 100

int    clock_gettime(clockid_t clockid, struct timespec *ts);
int    clock_settime(clockid_t clockid, const struct timespec *ts);
int    clock_getres(clockid_t clockid, struct timespec *res);
int    gettimeofday(struct timeval *tv, struct timezone *tz);
int    nanosleep(const struct timespec *req, struct timespec *rem);
time_t time(time_t *tloc);

clock_t clock(void);
double  difftime(time_t end, time_t start);

/* No timezone database: localtime == gmtime, mktime treats input as UTC. */
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime(const time_t *t);
struct tm *localtime_r(const time_t *t, struct tm *out);
time_t     mktime(struct tm *tm);
time_t     timegm(struct tm *tm);

/* asctime/ctime fill a static (or caller-supplied) 26-byte buffer of the
 * form "Wed Jun 30 21:49:08 1993\n". */
char *asctime(const struct tm *tm);
char *asctime_r(const struct tm *tm, char *buf);
char *ctime(const time_t *t);
char *ctime_r(const time_t *t, char *buf);

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

void  tzset(void);
extern char *tzname[2];
extern long  timezone;
extern int   daylight;
