#include <string.h>
#include <strings.h>
#include <ctype.h>

/* Extension functions beyond C99 — common GNU/BSD/musl additions that
 * BusyBox and ports rely on. Implementations follow musl semantics. */

char *strsep(char **stringp, const char *delim) {
    char *s = *stringp;
    if (!s) return 0;
    char *p = s + strcspn(s, delim);
    if (*p) {
        *p = 0;
        *stringp = p + 1;
    } else {
        *stringp = 0;
    }
    return s;
}

char *strchrnul(const char *s, int c) {
    c = (unsigned char)c;
    if (!c) return (char *)s + strlen(s);
    for (; *s && (unsigned char)*s != c; s++);
    return (char *)s;
}

void *memrchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s + n;
    while (n--) if (*--p == (unsigned char)c) return (void *)p;
    return 0;
}

void *mempcpy(void *dst, const void *src, size_t n) {
    return (char *)memcpy(dst, src, n) + n;
}

char *stpcpy(char *dst, const char *src) {
    while ((*dst = *src)) { dst++; src++; }
    return dst;
}

char *stpncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    char *end = dst + i;
    for (; i < n; i++) dst[i] = 0;
    return end;
}

char *strcasestr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    size_t nl = strlen(needle);
    for (; *hay; hay++) if (strncasecmp(hay, needle, nl) == 0) return (char *)hay;
    return 0;
}

/* strverscmp: like strcmp, but treats embedded digit runs as numbers so
 * "file9" sorts before "file10". Matches GNU semantics. */
int strverscmp(const char *a, const char *b) {
    const unsigned char *l = (const unsigned char *)a;
    const unsigned char *r = (const unsigned char *)b;
    size_t i = 0;
    while (l[i] == r[i] && l[i]) i++;
    /* find common digit-run start */
    size_t dp = i;
    while (dp > 0 && isdigit(l[dp - 1])) dp--;
    if (!isdigit(l[dp]) || !isdigit(r[dp])) return l[i] - r[i];
    /* leading zero → fractional compare (lexicographic) */
    if (l[dp] == '0' || r[dp] == '0') return l[i] - r[i];
    /* otherwise numeric: longer digit run is larger */
    size_t la = 0, lb = 0;
    while (isdigit(l[dp + la])) la++;
    while (isdigit(r[dp + lb])) lb++;
    if (la != lb) return (int)la - (int)lb;
    return l[i] - r[i];
}
