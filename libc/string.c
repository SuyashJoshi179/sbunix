#include <string.h>

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) {
        return dest;
    }

    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *d = (unsigned char *)s;
    unsigned char v = (unsigned char)c;

    for (size_t i = 0; i < n; i++) {
        d[i] = v;
    }

    return s;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) {
            return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
        }
        i++;
    }
    return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
            return (int)((unsigned char)s1[i] - (unsigned char)s2[i]);
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    size_t i = 0;
    while (1) {
        dest[i] = src[i];
        if (src[i] == '\0') {
            return dest;
        }
        i++;
    }
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    for (; i < n; i++) {
        dest[i] = '\0';
    }

    return dest;
}
