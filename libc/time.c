#include <time.h>
#include <sys/times.h>
#include <stdint.h>
#include <stddef.h>
#include "syscall_priv.h"

static long ecall1(long num, long a0) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}

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

int clock_settime(int clockid, const struct timespec *ts) {
    return (int)syscall_ret(ecall2(88, (long)clockid, (long)ts));
}

int clock_getres(int clockid, struct timespec *res) {
    return (int)syscall_ret(ecall2(89, (long)clockid, (long)res));
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
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

clock_t times(struct tms *buf) {
    /* Kernel returns ticks since boot in a0 and fills the four counters
     * in *buf (a struct k_tms with the same layout as struct tms). On
     * failure (-EFAULT for a bad pointer) propagate (clock_t)-1. */
    long r = ecall1(116 /* SYS_times */, (long)buf);
    if (r < 0) return (clock_t)-1;
    return (clock_t)r;
}

/* ------------------------------------------------------------------
 * POSIX time conversion. UTC only — no timezone database. localtime is
 * therefore identical to gmtime, and mktime treats its argument as UTC.
 * Calendar math uses Howard Hinnant's civil_from_days / days_from_civil
 * (proleptic Gregorian, valid across the full int64 epoch range).
 * ------------------------------------------------------------------ */

clock_t clock(void) {
    return times(0);
}

double difftime(time_t end, time_t start) {
    return (double)(end - start);
}

static void civil_from_days(long z, int *yy, unsigned *mm, unsigned *dd) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yr = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *dd = doy - (153 * mp + 2) / 5 + 1;
    *mm = mp < 10 ? mp + 3 : mp - 9;
    *yy = (int)(yr + (*mm <= 2 ? 1 : 0));
}

static long days_from_civil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static int day_of_year(int y, unsigned m, unsigned d) {
    static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int leap = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 1 : 0;
    int yday = cum[m - 1] + (int)d - 1;
    if (m > 2) yday += leap;
    return yday;
}

struct tm *gmtime_r(const time_t *t, struct tm *out) {
    if (!t || !out) return 0;
    long long secs = (long long)*t;
    long days = (long)(secs / 86400);
    long sod  = (long)(secs % 86400);
    if (sod < 0) { sod += 86400; days -= 1; }

    int y; unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);

    out->tm_sec   = (int)(sod % 60);
    out->tm_min   = (int)((sod / 60) % 60);
    out->tm_hour  = (int)(sod / 3600);
    out->tm_mday  = (int)d;
    out->tm_mon   = (int)mo - 1;
    out->tm_year  = y - 1900;
    /* 1970-01-01 was a Thursday (=4). */
    long w = (days % 7 + 4) % 7;
    if (w < 0) w += 7;
    out->tm_wday  = (int)w;
    out->tm_yday  = day_of_year(y, mo, d);
    out->tm_isdst = 0;
    return out;
}

static struct tm gmtime_buf;

struct tm *gmtime(const time_t *t) {
    return gmtime_r(t, &gmtime_buf);
}

struct tm *localtime_r(const time_t *t, struct tm *out) {
    return gmtime_r(t, out);
}

struct tm *localtime(const time_t *t) {
    return gmtime(t);
}

time_t timegm(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int y  = tm->tm_year + 1900;
    int mo = tm->tm_mon + 1;
    /* Normalize month into 1..12 by shifting overflow into year. */
    while (mo > 12) { mo -= 12; y++; }
    while (mo < 1)  { mo += 12; y--; }
    /* Normalize tm_mday by anchoring at day 1 of the month and applying
     * (mday - 1) as a signed offset, so values <= 0 roll into the previous
     * month and large values roll forward — matches POSIX mktime. */
    long days = days_from_civil(y, (unsigned)mo, 1)
              + (long)tm->tm_mday - 1;
    long long secs = (long long)days * 86400
                   + (long long)tm->tm_hour * 3600
                   + (long long)tm->tm_min  * 60
                   + (long long)tm->tm_sec;
    /* Refresh wday/yday for caller convenience. */
    time_t result = (time_t)secs;
    gmtime_r(&result, tm);
    return result;
}

time_t mktime(struct tm *tm) {
    return timegm(tm);
}

static const char *wday_short[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *wday_long[]  = {"Sunday","Monday","Tuesday","Wednesday",
                                   "Thursday","Friday","Saturday"};
static const char *mon_short[]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
static const char *mon_long[]   = {"January","February","March","April","May",
                                   "June","July","August","September","October",
                                   "November","December"};

static char *put_str(char *dst, char *end, const char *s) {
    while (*s && dst < end) *dst++ = *s++;
    return dst;
}

/* dst is advanced unconditionally; bytes written only while dst < end. The
 * caller can detect overflow by comparing the returned dst to end. */
static char *put_pad(char *dst, char *end, int v, int width, char pad) {
    char tmp[16];
    int n = 0;
    if (v < 0) {
        if (dst < end) *dst = '-';
        dst++;
        v = -v;
    }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n < width) tmp[n++] = pad;
    while (n > 0) {
        if (dst < end) *dst = tmp[n - 1];
        dst++;
        n--;
    }
    return dst;
}

static char *put1(char *dst, char *end, char c) {
    if (dst < end) *dst = c;
    return dst + 1;
}

static char *put_ll(char *dst, char *end, long long v) {
    char tmp[24];
    int n = 0;
    unsigned long long uv;
    if (v < 0) {
        if (dst < end) *dst = '-';
        dst++;
        uv = (unsigned long long)-(v + 1) + 1ULL;
    } else {
        uv = (unsigned long long)v;
    }
    do { tmp[n++] = (char)('0' + uv % 10); uv /= 10; } while (uv);
    while (n > 0) {
        if (dst < end) *dst = tmp[n - 1];
        dst++;
        n--;
    }
    return dst;
}

char *asctime_r(const struct tm *tm, char *buf) {
    if (!tm || !buf) return 0;
    int wday = tm->tm_wday & 7;
    int mon  = tm->tm_mon;
    if (wday < 0 || wday > 6) wday = 0;
    if (mon < 0 || mon > 11) mon = 0;
    /* "Wed Jun 30 21:49:08 1993\n\0" — exactly 26 bytes. */
    char *p = buf;
    char *e = buf + 26;
    p = put_str(p, e, wday_short[wday]);
    if (p < e) *p++ = ' ';
    p = put_str(p, e, mon_short[mon]);
    if (p < e) *p++ = ' ';
    p = put_pad(p, e, tm->tm_mday, 2, ' ');
    if (p < e) *p++ = ' ';
    p = put_pad(p, e, tm->tm_hour, 2, '0');
    if (p < e) *p++ = ':';
    p = put_pad(p, e, tm->tm_min,  2, '0');
    if (p < e) *p++ = ':';
    p = put_pad(p, e, tm->tm_sec,  2, '0');
    if (p < e) *p++ = ' ';
    p = put_pad(p, e, tm->tm_year + 1900, 4, '0');
    if (p < e) *p++ = '\n';
    if (p < e) *p   = '\0';
    else buf[25] = '\0';
    return buf;
}

static char asctime_buf[26];

char *asctime(const struct tm *tm) {
    return asctime_r(tm, asctime_buf);
}

char *ctime_r(const time_t *t, char *buf) {
    struct tm tm;
    if (!gmtime_r(t, &tm)) return 0;
    return asctime_r(&tm, buf);
}

char *ctime(const time_t *t) {
    return ctime_r(t, asctime_buf);
}

/* ------------------------------------------------------------------
 * strftime — supports the conversion specifiers most ported code uses:
 *   %% %n %t %Y %y %C %m %d %e %j %H %I %M %S %p %P %a %A %b %B %h
 *   %u %w %s %F (=%Y-%m-%d) %T (=%H:%M:%S) %R (=%H:%M) %D (=%m/%d/%y)
 *   %r (=%I:%M:%S %p) %c (POSIX C locale: %a %b %e %H:%M:%S %Y)
 * Width-modifier flags between '%' and the conversion are ignored.
 * Anything else copies the literal '%' + char.
 * ------------------------------------------------------------------ */
static size_t emit(char **pp, char *end, const char *s) {
    size_t n = 0;
    while (*s) {
        if (*pp >= end) { (*pp)++; n++; s++; continue; }
        **pp = *s++;
        (*pp)++;
        n++;
    }
    return n;
}

static size_t emit_pad(char **pp, char *end, int v, int width, char pad) {
    char *before = *pp;
    *pp = put_pad(*pp, end, v, width, pad);
    return (size_t)(*pp - before);
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    if (!s || !fmt || !tm || max == 0) return 0;
    char *p   = s;
    char *end = s + max - 1;   /* leave room for NUL */
    while (*fmt) {
        if (*fmt != '%') {
            p = put1(p, end, *fmt);
            fmt++;
            continue;
        }
        fmt++;
        /* Skip POSIX flag/width modifiers we don't honour. */
        while (*fmt == '-' || *fmt == '_' || *fmt == '0' ||
               *fmt == '^' || *fmt == '#' ||
               (*fmt >= '0' && *fmt <= '9')) fmt++;
        if (*fmt == 'E' || *fmt == 'O') fmt++;
        char c = *fmt++;
        switch (c) {
        case '%': p = put1(p, end, '%');  break;
        case 'n': p = put1(p, end, '\n'); break;
        case 't': p = put1(p, end, '\t'); break;
        case 'Y': emit_pad(&p, end, tm->tm_year + 1900, 4, '0'); break;
        case 'y': emit_pad(&p, end, (tm->tm_year + 1900) % 100, 2, '0'); break;
        case 'C': emit_pad(&p, end, (tm->tm_year + 1900) / 100, 2, '0'); break;
        case 'm': emit_pad(&p, end, tm->tm_mon + 1, 2, '0'); break;
        case 'd': emit_pad(&p, end, tm->tm_mday,    2, '0'); break;
        case 'e': emit_pad(&p, end, tm->tm_mday,    2, ' '); break;
        case 'j': emit_pad(&p, end, tm->tm_yday + 1, 3, '0'); break;
        case 'H': emit_pad(&p, end, tm->tm_hour, 2, '0'); break;
        case 'I': {
            int h = tm->tm_hour % 12; if (h == 0) h = 12;
            emit_pad(&p, end, h, 2, '0');
            break;
        }
        case 'M': emit_pad(&p, end, tm->tm_min, 2, '0'); break;
        case 'S': emit_pad(&p, end, tm->tm_sec, 2, '0'); break;
        case 'p': emit(&p, end, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'P': emit(&p, end, tm->tm_hour < 12 ? "am" : "pm"); break;
        case 'a': {
            int w = tm->tm_wday & 7; if (w > 6) w = 0;
            emit(&p, end, wday_short[w]);
            break;
        }
        case 'A': {
            int w = tm->tm_wday & 7; if (w > 6) w = 0;
            emit(&p, end, wday_long[w]);
            break;
        }
        case 'b':
        case 'h': {
            int m = tm->tm_mon; if (m < 0 || m > 11) m = 0;
            emit(&p, end, mon_short[m]);
            break;
        }
        case 'B': {
            int m = tm->tm_mon; if (m < 0 || m > 11) m = 0;
            emit(&p, end, mon_long[m]);
            break;
        }
        case 'u': {
            int w = tm->tm_wday == 0 ? 7 : tm->tm_wday;
            emit_pad(&p, end, w, 1, '0');
            break;
        }
        case 'w': emit_pad(&p, end, tm->tm_wday, 1, '0'); break;
        case 's': {
            struct tm copy = *tm;
            time_t t = timegm(&copy);
            p = put_ll(p, end, (long long)t);
            break;
        }
        case 'F':
            emit_pad(&p, end, tm->tm_year + 1900, 4, '0');
            p = put1(p, end, '-');
            emit_pad(&p, end, tm->tm_mon + 1, 2, '0');
            p = put1(p, end, '-');
            emit_pad(&p, end, tm->tm_mday, 2, '0');
            break;
        case 'T':
            emit_pad(&p, end, tm->tm_hour, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_min, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_sec, 2, '0');
            break;
        case 'R':
            emit_pad(&p, end, tm->tm_hour, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_min, 2, '0');
            break;
        case 'D':
            emit_pad(&p, end, tm->tm_mon + 1, 2, '0');
            p = put1(p, end, '/');
            emit_pad(&p, end, tm->tm_mday, 2, '0');
            p = put1(p, end, '/');
            emit_pad(&p, end, (tm->tm_year + 1900) % 100, 2, '0');
            break;
        case 'r': {
            int h = tm->tm_hour % 12; if (h == 0) h = 12;
            emit_pad(&p, end, h, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_min, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_sec, 2, '0');
            p = put1(p, end, ' ');
            emit(&p, end, tm->tm_hour < 12 ? "AM" : "PM");
            break;
        }
        /* POSIX C locale: "%a %b %e %H:%M:%S %Y" — identical to ctime
         * without the trailing newline. We don't ship locales so this
         * is the only representation %c can produce. */
        case 'c': {
            int w = tm->tm_wday & 7; if (w > 6) w = 0;
            int mo = tm->tm_mon;     if (mo < 0 || mo > 11) mo = 0;
            emit(&p, end, wday_short[w]);
            p = put1(p, end, ' ');
            emit(&p, end, mon_short[mo]);
            p = put1(p, end, ' ');
            emit_pad(&p, end, tm->tm_mday, 2, ' ');
            p = put1(p, end, ' ');
            emit_pad(&p, end, tm->tm_hour, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_min, 2, '0');
            p = put1(p, end, ':');
            emit_pad(&p, end, tm->tm_sec, 2, '0');
            p = put1(p, end, ' ');
            emit_pad(&p, end, tm->tm_year + 1900, 4, '0');
            break;
        }
        default:
            p = put1(p, end, '%');
            if (c) p = put1(p, end, c);
            break;
        }
    }
    /* POSIX: return 0 (and leave contents indeterminate) when the result
     * does not fit in `max` bytes including the terminating NUL. */
    if (p > end) {
        s[max - 1] = '\0';
        return 0;
    }
    *p = '\0';
    return (size_t)(p - s);
}

/* No timezone database. Pretend everyone is UTC. */
static char tz_utc[] = "UTC";
char *tzname[2] = { tz_utc, tz_utc };
long  timezone  = 0;
int   daylight  = 0;

void tzset(void) { /* no-op */ }
