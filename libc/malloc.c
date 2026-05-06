#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define ALIGN       16
#define HDR_SIZE    (sizeof(struct chunk))
#define MIN_ALLOC   4096

#define USED_BIT    1UL

struct chunk {
    unsigned long size;
    struct chunk *next;
};

static struct chunk *free_list;

static unsigned long align_up(unsigned long n, unsigned long a) {
    return (n + a - 1) & ~(a - 1);
}

static struct chunk *grow_heap(unsigned long need) {
    unsigned long total = need + HDR_SIZE;
    if (total < MIN_ALLOC) total = MIN_ALLOC;
    total = align_up(total, MIN_ALLOC);

    void *p = sbrk((long)total);
    if ((long)p < 0) return 0;

    struct chunk *c = (struct chunk *)p;
    c->size = total - HDR_SIZE;
    c->next = 0;
    free(((char *)c) + HDR_SIZE);
    return free_list;
}

void *malloc(unsigned long size) {
    if (size == 0) return 0;

    size = align_up(size, ALIGN);

    struct chunk *prev = 0;
    struct chunk *best = 0;
    struct chunk *best_prev = 0;

    for (struct chunk *c = free_list; c; c = c->next) {
        if (c->size >= size) {
            if (!best || c->size < best->size) {
                best = c;
                best_prev = prev;
            }
        }
        prev = c;
    }

    if (!best) {
        if (!grow_heap(size)) return 0;
        prev = 0;
        best = 0;
        for (struct chunk *c = free_list; c; c = c->next) {
            if (c->size >= size) {
                if (!best || c->size < best->size) {
                    best = c;
                    best_prev = prev;
                }
            }
            prev = c;
        }
        if (!best) return 0;
    }

    if (best->size >= size + HDR_SIZE + ALIGN) {
        struct chunk *rest = (struct chunk *)((char *)best + HDR_SIZE + size);
        rest->size = best->size - size - HDR_SIZE;
        rest->next = best->next;
        best->size = size;
        if (best_prev) best_prev->next = rest;
        else free_list = rest;
    } else {
        if (best_prev) best_prev->next = best->next;
        else free_list = best->next;
    }

    best->size |= USED_BIT;
    best->next = 0;
    return (char *)best + HDR_SIZE;
}

void free(void *ptr) {
    if (!ptr) return;
    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);
    c->size &= ~USED_BIT;

    struct chunk *prev = 0;
    struct chunk *cur = free_list;
    while (cur && cur < c) {
        prev = cur;
        cur = cur->next;
    }

    if (cur && (char *)c + HDR_SIZE + c->size == (char *)cur) {
        c->size += HDR_SIZE + cur->size;
        c->next = cur->next;
    } else {
        c->next = cur;
    }

    if (prev && (char *)prev + HDR_SIZE + prev->size == (char *)c) {
        prev->size += HDR_SIZE + c->size;
        prev->next = c->next;
    } else if (prev) {
        prev->next = c;
    } else {
        free_list = c;
    }
}

void *calloc(unsigned long nmemb, unsigned long size) {
    unsigned long total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return 0;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, unsigned long size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);
    unsigned long old_size = c->size & ~USED_BIT;
    if (old_size >= size) return ptr;

    void *newp = malloc(size);
    if (!newp) return 0;
    memcpy(newp, ptr, old_size);
    free(ptr);
    return newp;
}
