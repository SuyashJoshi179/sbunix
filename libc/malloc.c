#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

/* Single huge mmap'd arena placed at HEAP_ARENA_BASE via MAP_FIXED.
 * Bump pointer + best-fit free list inside. Allocations >= DIRECT_THRESHOLD
 * bypass the arena and use a per-call mmap; their free() returns physical
 * pages to the OS immediately via munmap. Within the arena, free()
 * coalesces but does not return physical pages to the OS (future TODO:
 * madvise(MADV_DONTNEED)). */

#define ALIGN              16
#define HDR_SIZE           (sizeof(struct chunk))
#define USED_BIT           1UL
#define DIRECT_BIT         2UL
#define DIRECT_THRESHOLD   (16UL * 1024 * 1024)

#define HEAP_ARENA_BASE    0x0000000100000000UL  /* keep in sync with kernel */
#define HEAP_ARENA_END     0x0000001000000000UL
#define ARENA_SIZE         (HEAP_ARENA_END - HEAP_ARENA_BASE)

struct chunk {
    unsigned long size;       /* low bit USED, bit 1 DIRECT             */
    struct chunk *next;       /* free-list link when free               */
};

static char         *arena_base;
static char         *arena_top;
static char         *arena_end;
static int           arena_inited;
static struct chunk *free_list;

/* Returns 0 if rounding up would wrap past ULONG_MAX. Callers must
 * treat 0 as overflow (malloc(0) is already handled separately). */
static unsigned long align_up(unsigned long n, unsigned long a) {
    if (n > (unsigned long)-1 - (a - 1)) return 0;
    return (n + a - 1) & ~(a - 1);
}

static int arena_init(void) {
    if (arena_inited) return 0;
    void *p = mmap((void *)HEAP_ARENA_BASE, (long)ARENA_SIZE,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return -1;
    arena_base = (char *)p;
    arena_top  = arena_base;
    arena_end  = arena_base + ARENA_SIZE;
    arena_inited = 1;
    return 0;
}

static void *arena_alloc(unsigned long size) {
    struct chunk *prev = 0, *best = 0, *best_prev = 0;
    for (struct chunk *c = free_list; c; c = c->next) {
        if (c->size >= size) {
            if (!best || c->size < best->size) { best = c; best_prev = prev; }
        }
        prev = c;
    }
    if (best) {
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
    unsigned long need = HDR_SIZE + size;
    if ((unsigned long)(arena_end - arena_top) < need) return 0;
    struct chunk *c = (struct chunk *)arena_top;
    arena_top += need;
    c->size = size | USED_BIT;
    c->next = 0;
    return (char *)c + HDR_SIZE;
}

static void *direct_alloc(unsigned long size) {
    unsigned long total = HDR_SIZE + size;
    void *p = mmap(0, (long)total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return 0;
    struct chunk *c = (struct chunk *)p;
    c->size = size | USED_BIT | DIRECT_BIT;
    c->next = 0;
    return (char *)c + HDR_SIZE;
}

void *malloc(unsigned long size) {
    if (size == 0) return 0;
    size = align_up(size, ALIGN);
    if (size == 0) return 0;                       /* align overflow */
    if (size > (unsigned long)-1 - HDR_SIZE) return 0; /* size+HDR wrap */

    if (size + HDR_SIZE >= DIRECT_THRESHOLD)
        return direct_alloc(size);

    if (arena_init() < 0) return 0;
    return arena_alloc(size);
}

void free(void *ptr) {
    if (!ptr) return;
    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);

    if (c->size & DIRECT_BIT) {
        unsigned long size = c->size & ~(USED_BIT | DIRECT_BIT);
        munmap(c, (long)(HDR_SIZE + size));
        return;
    }

    c->size &= ~USED_BIT;

    struct chunk *prev = 0, *cur = free_list;
    while (cur && cur < c) { prev = cur; cur = cur->next; }

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
    if (!p) return 0;
    memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, unsigned long size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    struct chunk *c = (struct chunk *)((char *)ptr - HDR_SIZE);
    unsigned long old_size = c->size & ~(USED_BIT | DIRECT_BIT);
    unsigned long want = align_up(size, ALIGN);
    if (want == 0) return 0;
    if (old_size >= want) return ptr;

    /* In-place grow paths (arena-only — direct mmap'd blocks need a real
     * remap which we don't have, so they fall through to malloc+copy):
     *   1. If this chunk sits at arena_top, just bump arena_top.
     *   2. If the next free-list chunk is contiguous and the combined
     *      size is enough, absorb it (and split the remainder back into
     *      the free list if there is one). */
    if (!(c->size & DIRECT_BIT)) {
        char *c_end = (char *)c + HDR_SIZE + old_size;

        if (c_end == arena_top) {
            unsigned long extra = want - old_size;
            if ((unsigned long)(arena_end - arena_top) >= extra) {
                arena_top += extra;
                c->size = (want) | USED_BIT;
                return ptr;
            }
        }

        struct chunk *prev = 0, *cur = free_list;
        while (cur && (char *)cur < c_end) { prev = cur; cur = cur->next; }
        if (cur && (char *)cur == c_end) {
            unsigned long combined = old_size + HDR_SIZE + cur->size;
            if (combined >= want) {
                unsigned long leftover = combined - want;
                if (leftover >= HDR_SIZE + ALIGN) {
                    /* Split: keep the front, return the tail to the free list. */
                    struct chunk *tail = (struct chunk *)((char *)c + HDR_SIZE + want);
                    tail->size = leftover - HDR_SIZE;
                    tail->next = cur->next;
                    if (prev) prev->next = tail;
                    else      free_list  = tail;
                    c->size = want | USED_BIT;
                } else {
                    /* Absorb fully. */
                    if (prev) prev->next = cur->next;
                    else      free_list  = cur->next;
                    c->size = combined | USED_BIT;
                }
                return ptr;
            }
        }
    }

    void *newp = malloc(size);
    if (!newp) return 0;
    memcpy(newp, ptr, old_size);
    free(ptr);
    return newp;
}
