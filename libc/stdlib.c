#include <stdlib.h>
#include <signal.h>

void _Exit(int status) { exit(status); }

void abort(void) {
    raise(SIGABRT);
    exit(128 + 6);
}

int atoi(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    int n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return sign * n;
}

long atol(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    long sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    long n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return sign * n;
}

long long atoll(const char *s) { return (long long)atol(s); }

static int digit_val(char c, int base) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
    else return -1;
    return v < base ? v : -1;
}

long strtol(const char *s, char **endp, int base) {
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2; base = 16;
    } else if (base == 0 && *p == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    long n = 0; int any = 0; int d;
    while ((d = digit_val(*p, base)) >= 0) { n = n * base + d; p++; any = 1; }
    if (endp) *endp = (char *)(any ? p : s);
    return sign * n;
}

unsigned long strtoul(const char *s, char **endp, int base) {
    const char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2; base = 16;
    } else if (base == 0 && *p == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    unsigned long n = 0; int any = 0; int d;
    while ((d = digit_val(*p, base)) >= 0) { n = n * (unsigned)base + (unsigned)d; p++; any = 1; }
    if (endp) *endp = (char *)(any ? p : s);
    return n;
}

int abs(int x)   { return x < 0 ? -x : x; }
long labs(long x){ return x < 0 ? -x : x; }

static unsigned long _rand_state = 1;
int rand(void) {
    _rand_state = _rand_state * 1103515245UL + 12345UL;
    return (int)((_rand_state >> 16) & 0x7fffffff);
}
void srand(unsigned seed) { _rand_state = seed; }

char *getenv(const char *name) { (void)name; return 0; }

int atexit(void (*func)(void)) { (void)func; return 0; }
