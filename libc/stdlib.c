#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

void _Exit(int status) { exit(status); }

void abort(void) {
    raise(SIGABRT);
    exit(128 + 6);
}

/* assert() in <assert.h> calls this on a failed predicate. We can't depend
 * on stdio (assert may fire before its setup is reasonable), so write
 * directly to stderr (fd 2) and exit. */
static void _assert_write(const char *s) {
    if (!s) return;
    long n = 0; while (s[n]) n++;
    write(2, s, n);
}
static void _assert_writel(long v) {
    char buf[24]; int i = 0;
    if (v < 0) { write(2, "-", 1); v = -v; }
    if (v == 0) buf[i++] = '0';
    else while (v) { buf[i++] = '0' + (int)(v % 10); v /= 10; }
    while (i) { char c = buf[--i]; write(2, &c, 1); }
}

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    _assert_write(file);
    _assert_write(":");
    _assert_writel(line);
    _assert_write(": ");
    _assert_write(func);
    _assert_write(": Assertion `");
    _assert_write(expr);
    _assert_write("' failed.\n");
    abort();
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

double atof(const char *s) { (void)s; return 0.0; }

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

long long strtoll(const char *s, char **endp, int base) {
    return (long long)strtol(s, endp, base);
}

unsigned long long strtoull(const char *s, char **endp, int base) {
    return (unsigned long long)strtoul(s, endp, base);
}

double strtod(const char *s, char **endp) {
    if (endp) *endp = (char *)s;
    return 0.0;
}

float strtof(const char *s, char **endp) {
    if (endp) *endp = (char *)s;
    return 0.0f;
}

int abs(int x)         { return x < 0 ? -x : x; }
long labs(long x)      { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int num, int den) {
    div_t r = { num / den, num % den };
    return r;
}
ldiv_t ldiv(long num, long den) {
    ldiv_t r = { num / den, num % den };
    return r;
}
lldiv_t lldiv(long long num, long long den) {
    lldiv_t r = { num / den, num % den };
    return r;
}

int    setenv(const char *name, const char *value, int overwrite) {
    (void)name; (void)value; (void)overwrite; return 0;
}
int    unsetenv(const char *name) { (void)name; return 0; }
int    putenv(char *string)       { (void)string; return 0; }
int    system(const char *cmd)    { (void)cmd; return -1; }

int    mblen(const char *s, size_t n) {
    if (!s) return 0;
    if (n == 0) return -1;
    return *s ? 1 : 0;
}
int    mbtowc(int *pwc, const char *s, size_t n) {
    if (!s) return 0;
    if (n == 0) return -1;
    if (pwc) *pwc = (unsigned char)*s;
    return *s ? 1 : 0;
}
int    wctomb(char *s, int wc) {
    if (!s) return 0;
    *s = (char)wc;
    return 1;
}
size_t mbstowcs(int *pwcs, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n && s[i]; i++) if (pwcs) pwcs[i] = (unsigned char)s[i];
    return i;
}
size_t wcstombs(char *s, const int *pwcs, size_t n) {
    size_t i;
    for (i = 0; i < n && pwcs[i]; i++) if (s) s[i] = (char)pwcs[i];
    return i;
}

static unsigned long _rand_state = 1;
int rand(void) {
    _rand_state = _rand_state * 1103515245UL + 12345UL;
    return (int)((_rand_state >> 16) & 0x7fffffff);
}
void srand(unsigned seed) { _rand_state = seed; }

char *getenv(const char *name) { (void)name; return 0; }

int atexit(void (*func)(void)) { (void)func; return 0; }

/* Insertion sort — fine for small N; user code should not feed huge arrays. */
void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *)) {
    if (!base || nmemb < 2 || size == 0 || !cmp) return;
    char *a = base;
    char *tmp = (char *)malloc(size);
    if (!tmp) return;
    for (size_t i = 1; i < nmemb; i++) {
        memcpy(tmp, a + i * size, size);
        size_t j = i;
        while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0) {
            memcpy(a + j * size, a + (j - 1) * size, size);
            j--;
        }
        memcpy(a + j * size, tmp, size);
    }
    free(tmp);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
    if (!key || !base || size == 0 || !cmp) return 0;
    size_t lo = 0, hi = nmemb;
    const char *a = base;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, a + mid * size);
        if (c == 0) return (void *)(a + mid * size);
        if (c < 0) hi = mid;
        else       lo = mid + 1;
    }
    return 0;
}

intmax_t imaxabs(intmax_t x) { return x < 0 ? -x : x; }

imaxdiv_t imaxdiv(intmax_t num, intmax_t den) {
    imaxdiv_t r = { num / den, num % den };
    return r;
}

intmax_t strtoimax(const char *s, char **endp, int base) {
    return (intmax_t)strtol(s, endp, base);
}

uintmax_t strtoumax(const char *s, char **endp, int base) {
    return (uintmax_t)strtoul(s, endp, base);
}
