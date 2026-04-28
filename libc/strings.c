#include <strings.h>
#include <string.h>
#include <ctype.h>

void bzero(void *s, size_t n) {
    memset(s, 0, n);
}

int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

void bcopy(const void *src, void *dst, size_t n) {
    memmove(dst, src, n);
}

int ffs(int x) {
    if (x == 0) return 0;
    int n = 1;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
}

int ffsl(long x) {
    if (x == 0) return 0;
    int n = 1;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
}

int ffsll(long long x) {
    if (x == 0) return 0;
    int n = 1;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
}

int strcasecmp(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}
