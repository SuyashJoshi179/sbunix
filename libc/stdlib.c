#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>

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
    /* Accumulate as unsigned. The positive ceiling is LONG_MAX; the
     * negative ceiling is |LONG_MIN| which is one larger and only
     * representable as unsigned. cutoff/cutlim split the ceiling into
     * "quotient by base" and "remainder by base" so the overflow check
     * is a single compare per digit. */
    unsigned long max_abs = sign > 0 ? (unsigned long)LONG_MAX
                                     : (unsigned long)LONG_MAX + 1;
    unsigned long cutoff = max_abs / (unsigned)base;
    unsigned long cutlim = max_abs % (unsigned)base;
    unsigned long n = 0; int any = 0, overflow = 0, d;
    while ((d = digit_val(*p, base)) >= 0) {
        if (overflow || n > cutoff || (n == cutoff && (unsigned long)d > cutlim))
            overflow = 1;
        else
            n = n * (unsigned)base + (unsigned)d;
        p++; any = 1;
    }
    if (endp) *endp = (char *)(any ? p : s);
    if (overflow) {
        errno = ERANGE;
        return sign > 0 ? LONG_MAX : LONG_MIN;
    }
    if (sign > 0) return (long)n;
    /* |LONG_MIN| is one past LONG_MAX and therefore not representable
     * as a signed long. Casting it and negating is implementation-
     * defined / UB respectively, so produce LONG_MIN directly. */
    if (n == (unsigned long)LONG_MAX + 1) return LONG_MIN;
    return -(long)n;
}

unsigned long strtoul(const char *s, char **endp, int base) {
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
    unsigned long cutoff = ULONG_MAX / (unsigned)base;
    unsigned long cutlim = ULONG_MAX % (unsigned)base;
    unsigned long n = 0; int any = 0, overflow = 0, d;
    while ((d = digit_val(*p, base)) >= 0) {
        if (overflow || n > cutoff || (n == cutoff && (unsigned long)d > cutlim))
            overflow = 1;
        else
            n = n * (unsigned)base + (unsigned)d;
        p++; any = 1;
    }
    if (endp) *endp = (char *)(any ? p : s);
    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    /* POSIX: a leading '-' produces the negated value mod 2^N — `-1`
     * round-trips to ULONG_MAX, not 0. */
    return sign > 0 ? n : -n;
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

/* Process environment storage.
 *
 * Initial state: `env_arr == NULL` and the global `environ` (defined in
 * libc/exec.c) points at the static empty array installed by crt. On
 * the first mutation we malloc a dynamic array, copy any pre-existing
 * entries from `environ` into it (marking them as not-owned so we
 * won't free caller-supplied strings), and switch `environ` to point
 * at the dynamic array.
 *
 * Ownership tracking matters because POSIX `putenv` installs the
 * caller's pointer directly (no copy), while `setenv` mallocs its
 * own buffer. On overwrite/unset, we only free strings we own. The
 * kernel does not propagate envp across execv so the array resets to
 * empty in every fresh process — by design. */
static char         **env_arr;
static unsigned char *env_owned;
static unsigned       env_len;
static unsigned       env_cap;

/* Ensure env_arr is allocated and has capacity for at least `want_cap`
 * pointer slots plus the trailing NULL terminator. Returns 0 on
 * success, -1 on allocation failure (caller sets errno). */
static int env_reserve(unsigned want_cap) {
    if (!env_arr) {
        unsigned n = 0;
        char **src = environ;
        if (src) while (src[n]) n++;
        unsigned cap = n + 8;
        if (cap < want_cap) cap = want_cap;
        char **na = malloc(sizeof(char *) * (cap + 1));
        if (!na) return -1;
        unsigned char *no = malloc(cap);
        if (!no) { free(na); return -1; }
        for (unsigned i = 0; i < n; i++) {
            na[i] = src[i];
            no[i] = 0;
        }
        na[n] = NULL;
        env_arr   = na;
        env_owned = no;
        env_len   = n;
        env_cap   = cap;
        environ   = env_arr;
    }
    if (env_cap >= want_cap) return 0;
    unsigned new_cap = env_cap * 2;
    if (new_cap < want_cap) new_cap = want_cap;
    /* Grow env_owned first. If env_arr's realloc then fails, env_owned
     * just has a few extra bytes — env_cap still matches the old caps
     * so no caller can observe a half-committed environ pointer. */
    unsigned char *no = realloc(env_owned, new_cap);
    if (!no) return -1;
    env_owned = no;
    char **na = realloc(env_arr, sizeof(char *) * (new_cap + 1));
    if (!na) return -1;
    env_arr   = na;
    environ   = env_arr;
    env_cap   = new_cap;
    return 0;
}

/* Linear scan of env_arr for an entry whose name (prefix before '=')
 * is exactly `name[0..nlen)`. Returns -1 if not found or not yet
 * initialized. */
static int env_find(const char *name, size_t nlen) {
    if (!env_arr) return -1;
    for (unsigned i = 0; i < env_len; i++) {
        if (strncmp(env_arr[i], name, nlen) == 0 && env_arr[i][nlen] == '=')
            return (int)i;
    }
    return -1;
}

/* Remove every entry from index `from` onward whose name matches
 * `name[0..nlen)`. Used to keep getenv() deterministic when the
 * caller-supplied initial environ contained duplicate names: setenv
 * and putenv overwrite the first match, then call this to strip the
 * remaining ones; unsetenv calls it with from=0 to remove them all. */
static void env_strip_dupes(unsigned from, const char *name, size_t nlen) {
    for (unsigned i = from; i < env_len; ) {
        if (strncmp(env_arr[i], name, nlen) == 0 && env_arr[i][nlen] == '=') {
            if (env_owned[i]) free(env_arr[i]);
            for (unsigned k = i; k < env_len - 1; k++) {
                env_arr[k]   = env_arr[k + 1];
                env_owned[k] = env_owned[k + 1];
            }
            env_len--;
            env_arr[env_len] = NULL;
        } else {
            i++;
        }
    }
}

char *getenv(const char *name) {
    if (!name || !*name) return NULL;
    size_t nlen = strlen(name);
    if (memchr(name, '=', nlen)) return NULL;
    char **e = environ;
    if (!e) return NULL;
    for (unsigned i = 0; e[i]; i++) {
        if (strncmp(e[i], name, nlen) == 0 && e[i][nlen] == '=')
            return e[i] + nlen + 1;
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!value) value = "";
    size_t nlen = strlen(name);

    /* If the var already exists and !overwrite, this is a no-op — scan
     * environ directly so we don't allocate dynamic storage to discover
     * that. */
    if (!overwrite) {
        char **e = environ;
        if (e) {
            for (unsigned i = 0; e[i]; i++) {
                if (strncmp(e[i], name, nlen) == 0 && e[i][nlen] == '=')
                    return 0;
            }
        }
    }

    if (env_reserve(env_len + 2) < 0) { errno = ENOMEM; return -1; }

    int idx = env_find(name, nlen);

    size_t vlen = strlen(value);
    char *entry = malloc(nlen + 1 + vlen + 1);
    if (!entry) { errno = ENOMEM; return -1; }
    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen + 1);

    if (idx >= 0) {
        if (env_owned[idx]) free(env_arr[idx]);
        env_arr[idx]   = entry;
        env_owned[idx] = 1;
        env_strip_dupes((unsigned)idx + 1, name, nlen);
    } else {
        env_arr[env_len]   = entry;
        env_owned[env_len] = 1;
        env_len++;
        env_arr[env_len]   = NULL;
    }
    return 0;
}

int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t nlen = strlen(name);

    /* If the var isn't present, unsetenv is a no-op — scan environ
     * directly so the no-op path doesn't allocate dynamic storage. */
    char **e = environ;
    if (!e) return 0;
    unsigned found = 0;
    for (unsigned i = 0; e[i]; i++) {
        if (strncmp(e[i], name, nlen) == 0 && e[i][nlen] == '=') {
            found = 1;
            break;
        }
    }
    if (!found) return 0;

    if (env_reserve(env_len + 1) < 0) { errno = ENOMEM; return -1; }
    /* POSIX: unsetenv removes every matching entry, not just the first. */
    env_strip_dupes(0, name, nlen);
    return 0;
}

/* POSIX putenv: install the caller's pointer directly. The caller
 * owns the storage and must not free or modify the string while it
 * remains in the environment. A glibc-compatible extension treats a
 * string without '=' as a request to remove the variable named by it.
 */
int putenv(char *string) {
    if (!string || !*string) { errno = EINVAL; return -1; }
    char *eq = strchr(string, '=');
    if (!eq) return unsetenv(string);
    if (eq == string) { errno = EINVAL; return -1; }
    if (env_reserve(env_len + 2) < 0) { errno = ENOMEM; return -1; }

    size_t nlen = (size_t)(eq - string);
    int idx = env_find(string, nlen);
    if (idx >= 0) {
        if (env_owned[idx]) free(env_arr[idx]);
        env_arr[idx]   = string;
        env_owned[idx] = 0;
        env_strip_dupes((unsigned)idx + 1, string, nlen);
    } else {
        env_arr[env_len]   = string;
        env_owned[env_len] = 0;
        env_len++;
        env_arr[env_len]   = NULL;
    }
    return 0;
}

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
