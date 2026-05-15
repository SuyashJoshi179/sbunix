#include <string.h>
#include <stdlib.h>

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) i++;
    return i;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* strcoll: POSIX collation. No locale support in this libc — treat as
 * strcmp, which is what POSIX-C-locale dictates anyway. */
int strcoll(const char *a, const char *b) {
    return strcmp(a, b);
}

/* strerror_r: thread-safe strerror. Single-threaded user code means
 * strerror is already safe; we still copy the string into the caller's
 * buffer to honor the contract. */
int strerror_r(int errnum, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return 22 /* EINVAL */;
    char *s = strerror(errnum);
    size_t i = 0;
    for (; i + 1 < buflen && s[i]; i++) buf[i] = s[i];
    buf[i] = 0;
    return 0;
}

/* strxfrm: POSIX collation transform. Without locale, the canonical
 * transform of a string is the string itself. */
size_t strxfrm(char *dst, const char *src, size_t n) {
    size_t slen = strlen(src);
    if (n > 0 && dst) {
        size_t i = 0;
        for (; i + 1 < n && i < slen; i++) dst[i] = src[i];
        dst[i] = 0;
    }
    return slen;
}

/* strlcpy: BSD/POSIX-2024. Always NUL-terminates if n > 0. Returns
 * src length (caller can detect truncation when retval >= n). */
size_t strlcpy(char *dst, const char *src, size_t n) {
    size_t slen = strlen(src);
    if (n > 0) {
        size_t copy = slen < n - 1 ? slen : n - 1;
        for (size_t i = 0; i < copy; i++) dst[i] = src[i];
        dst[copy] = 0;
    }
    return slen;
}

/* strlcat: BSD/POSIX-2024. Returns total intended length
 * (initial dst length + src length). */
size_t strlcat(char *dst, const char *src, size_t n) {
    size_t dlen = 0;
    while (dlen < n && dst[dlen]) dlen++;
    size_t slen = strlen(src);
    if (dlen == n) return n + slen;
    size_t room = n - dlen - 1;
    size_t copy = slen < room ? slen : room;
    for (size_t i = 0; i < copy; i++) dst[dlen + i] = src[i];
    dst[dlen + copy] = 0;
    return dlen + slen;
}

/* memccpy: copy up to n bytes, stopping just past the first occurrence
 * of c. Returns pointer one past the copied c, or NULL if c not found. */
void *memccpy(void *dst, const void *src, int c, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
        if (s[i] == uc) return d + i + 1;
    }
    return 0;
}

/* memmem: locate needle within haystack. GNU/BSD; added to POSIX-2024. */
void *memmem(const void *hay, size_t hlen, const void *needle, size_t nlen) {
    if (nlen == 0) return (void *)hay;
    if (nlen > hlen) return 0;
    const unsigned char *h = hay;
    const unsigned char *n = needle;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j = 0;
        while (j < nlen && h[i + j] == n[j]) j++;
        if (j == nlen) return (void *)(h + i);
    }
    return 0;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    if (c == 0) return (char *)s;
    return 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    for (; *s; s++)
        if (*s == (char)c) last = s;
    if (c == 0) return (char *)s;
    return (char *)last;
}

void *memset(void *dst, int c, size_t n) {
    char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (size_t i = 0; i < n; i++)
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    unsigned char ch = (unsigned char)c;
    for (size_t i = 0; i < n; i++)
        if (p[i] == ch) return (void *)(p + i);
    return 0;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) d++;
    size_t i = 0;
    while (i < n && src[i]) { d[i] = src[i]; i++; }
    d[i] = 0;
    return dst;
}

char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return 0;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++)
        for (const char *a = accept; *a; a++)
            if (*s == *a) return (char *)s;
    return 0;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    for (; s[n]; n++) {
        const char *a = accept;
        while (*a && *a != s[n]) a++;
        if (!*a) return n;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    for (; s[n]; n++) {
        for (const char *r = reject; *r; r++)
            if (s[n] == *r) return n;
    }
    return n;
}

char *strtok_r(char *s, const char *delim, char **saveptr) {
    if (!s) s = *saveptr;
    if (!s) return 0;
    s += strspn(s, delim);
    if (!*s) { *saveptr = 0; return 0; }
    char *tok = s;
    s += strcspn(s, delim);
    if (*s) { *s = 0; *saveptr = s + 1; }
    else    { *saveptr = 0; }
    return tok;
}

static char *_strtok_save;
char *strtok(char *s, const char *delim) {
    return strtok_r(s, delim, &_strtok_save);
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) return 0;
    memcpy(p, s, n);
    return p;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *p = malloc(len + 1);
    if (!p) return 0;
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

/* Static slot — async-signal-unsafe by design. Matches glibc's behavior. */
static char _strerror_buf[32];

char *strerror(int errnum) {
    static const char *const tab[] = {
        [0]  = "Success",
        [1]  = "Operation not permitted",
        [2]  = "No such file or directory",
        [3]  = "No such process",
        [4]  = "Interrupted system call",
        [5]  = "I/O error",
        [6]  = "No such device or address",
        [7]  = "Argument list too long",
        [8]  = "Exec format error",
        [9]  = "Bad file descriptor",
        [10] = "No child processes",
        [11] = "Resource temporarily unavailable",
        [12] = "Cannot allocate memory",
        [13] = "Permission denied",
        [14] = "Bad address",
        [16] = "Device or resource busy",
        [17] = "File exists",
        [18] = "Cross-device link",
        [19] = "No such device",
        [20] = "Not a directory",
        [21] = "Is a directory",
        [22] = "Invalid argument",
        [23] = "Too many open files in system",
        [24] = "Too many open files",
        [25] = "Inappropriate ioctl for device",
        [26] = "Text file busy",
        [27] = "File too large",
        [28] = "No space left on device",
        [29] = "Illegal seek",
        [30] = "Read-only file system",
        [31] = "Too many links",
        [32] = "Broken pipe",
        [33] = "Numerical argument out of domain",
        [34] = "Numerical result out of range",
        [36] = "File name too long",
        [38] = "Function not implemented",
        [39] = "Directory not empty",
        [40] = "Too many levels of symbolic links",
        [95] = "Operation not supported",
    };
    int n = errnum;
    if (n >= 0 && n < (int)(sizeof(tab) / sizeof(tab[0])) && tab[n])
        return (char *)tab[n];

    /* Fallback: "Unknown error N" without snprintf dependency. */
    char *p = _strerror_buf;
    const char *prefix = "Unknown error ";
    while (*prefix) *p++ = *prefix++;
    if (n < 0) { *p++ = '-'; n = -n; }
    char digits[12];
    int  d = 0;
    if (n == 0) digits[d++] = '0';
    else while (n) { digits[d++] = '0' + (n % 10); n /= 10; }
    while (d) *p++ = digits[--d];
    *p = 0;
    return _strerror_buf;
}
