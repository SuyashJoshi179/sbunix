#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>

// Convert days-since-1970-01-01 (signed) to Y-M-D using Howard Hinnant's
// civil_from_days. Valid for the full proleptic Gregorian range.
static void civil_from_days(long z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yr = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yr + (*m <= 2 ? 1 : 0));
}

static void put2(unsigned v) {
    char a = (char)('0' + (v / 10) % 10);
    char b = (char)('0' + v % 10);
    putchar(a); putchar(b);
}

static void put4(unsigned v) {
    putchar((char)('0' + (v / 1000) % 10));
    putchar((char)('0' + (v / 100) % 10));
    putchar((char)('0' + (v / 10) % 10));
    putchar((char)('0' + v % 10));
}

int main(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        fprintf(stderr, "date: clock_gettime failed: %s\n", strerror(errno));
        return 1;
    }
    long long secs = (long long)ts.tv_sec;
    long days = (long)(secs / 86400);
    long sod  = (long)(secs % 86400);
    if (sod < 0) { sod += 86400; days -= 1; }

    int y; unsigned mo, d;
    civil_from_days(days, &y, &mo, &d);
    unsigned hh = (unsigned)(sod / 3600);
    unsigned mm = (unsigned)((sod / 60) % 60);
    unsigned ss = (unsigned)(sod % 60);

    printf("epoch=%ld\n", (long)secs);
    put4((unsigned)y); putchar('-');
    put2(mo);          putchar('-');
    put2(d);           putchar(' ');
    put2(hh);          putchar(':');
    put2(mm);          putchar(':');
    put2(ss);
    printf(" UTC\n");
    return 0;
}
