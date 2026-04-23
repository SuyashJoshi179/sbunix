/*
 * zerofill_test — verify that pages returned by demand-paged regions
 * (anonymous mmap, grown heap, auto-grown stack) come back zeroed on
 * first read. A non-zero read would be a correctness bug and a kernel
 * memory disclosure.
 *
 * Three cases:
 *   1. mmap(MAP_ANON) — read every byte of every page before writing.
 *   2. sbrk grow       — ditto on freshly extended heap.
 *   3. Re-mmap         — munmap, re-mmap at same size, verify zeroed
 *                        (page might have been handed back via page_alloc).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define NPAGES    4

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[zerofill_test] PASS  %s\n", name);
    } else {
        printf("[zerofill_test] FAIL  %s\n", name);
        fails++;
    }
}

static int all_zero(const volatile char *p, long n) {
    for (long i = 0; i < n; i++)
        if (p[i] != 0) return 0;
    return 1;
}

int main(void) {
    printf("=== zerofill_test ===\n");

    /* Case 1: mmap anonymous */
    volatile char *m = mmap(0, NPAGES * PAGE_SIZE, PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)m > 0, "mmap anon succeeded");
    check(all_zero(m, NPAGES * PAGE_SIZE), "mmap anon pages are zero on first read");

    /* Dirty them so next allocation can't coincidentally reuse clean pages. */
    memset((void *)m, 0xAA, NPAGES * PAGE_SIZE);
    munmap((void *)m, NPAGES * PAGE_SIZE);

    /* Case 2: sbrk grow — pages come lazily through user_page_fault. */
    void *old = sbrk(NPAGES * PAGE_SIZE);
    check((long)old > 0, "sbrk grow succeeded");
    volatile char *h = (volatile char *)old;
    check(all_zero(h, NPAGES * PAGE_SIZE), "heap pages are zero on first read");
    sbrk(-(long)(NPAGES * PAGE_SIZE));

    /* Case 3: re-mmap — forces page_alloc to hand out pages that were just
     * freed. Must still be zero. */
    volatile char *m2 = mmap(0, NPAGES * PAGE_SIZE, PROT_READ | PROT_WRITE,
                             MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)m2 > 0, "second mmap succeeded");
    check(all_zero(m2, NPAGES * PAGE_SIZE),
          "recycled pages are zeroed before returning to user");
    munmap((void *)m2, NPAGES * PAGE_SIZE);

    printf("=== zerofill_test: %d failures ===\n", fails);
    return fails;
}
