#include <stdlib.h>
#include <string.h>

#define HEAP_SIZE (64 * 1024)

static unsigned char heap[HEAP_SIZE];
static size_t heap_used = 0;

void *malloc(size_t size) {
    if (size == 0) {
        return 0;
    }

    size = (size + 7) & ~(size_t)7;
    if (heap_used + size > HEAP_SIZE) {
        return 0;
    }

    void *ptr = &heap[heap_used];
    heap_used += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return 0;
    }

    if (nmemb > ((size_t)-1) / size) {
        return 0;
    }

    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr == 0) {
        return 0;
    }

    memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == 0) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return 0;
    }

    void *new_ptr = malloc(size);
    if (new_ptr == 0) {
        return 0;
    }

    return new_ptr;
}

int atoi(const char *nptr) {
    int sign = 1;
    int value = 0;

    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') {
        nptr++;
    }

    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        value = value * 10 + (*nptr - '0');
        nptr++;
    }

    return sign * value;
}

void abort(void) {
    while (1) {
    }
}
