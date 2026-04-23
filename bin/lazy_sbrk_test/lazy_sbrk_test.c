/*
 * lazy_sbrk_test — verify that sys_sbrk does not eagerly commit physical
 * pages. Growing the heap VMA must not reduce the free-page count; pages
 * are only allocated when user code touches them.
 *
 * Strategy:
 *   1. sbrk(0) to snapshot the initial brk.
 *   2. meminfo() for the baseline free-page count.
 *   3. sbrk(N * PAGE_SIZE) — pure VMA extension, no touches.
 *   4. meminfo() again — count must be identical to (2). No commit yet.
 *   5. Touch one byte in each page, one at a time, verifying meminfo drops
 *      by exactly one page per touch.
 *   6. sbrk(-N * PAGE_SIZE) — release. meminfo() must return to baseline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define NPAGES    8

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[lazy_sbrk_test] PASS  %s\n", name);
    } else {
        printf("[lazy_sbrk_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== lazy_sbrk_test ===\n");

    long baseline = meminfo();
    check(baseline > 0, "baseline meminfo > 0");

    void *old = sbrk(NPAGES * PAGE_SIZE);
    check((long)old > 0, "sbrk(NPAGES*PAGE_SIZE) succeeded");

    long after_grow = meminfo();
    check(after_grow == baseline,
          "meminfo unchanged after sbrk grow (lazy — no commit)");

    volatile char *base = (volatile char *)old;
    for (int i = 0; i < NPAGES; i++) {
        long before_touch = meminfo();
        base[i * PAGE_SIZE] = (char)('A' + i);
        long after_touch = meminfo();
        if (before_touch - after_touch != 1) {
            printf("  page %d: before=%ld after=%ld delta=%ld (want 1)\n",
                   i, before_touch, after_touch, before_touch - after_touch);
            fails++;
        } else {
            printf("[lazy_sbrk_test] PASS  page %d committed on first touch\n", i);
        }
    }

    for (int i = 0; i < NPAGES; i++) {
        check(base[i * PAGE_SIZE] == (char)('A' + i),
              "page content preserved after later touches");
    }

    void *ret = sbrk(-(long)(NPAGES * PAGE_SIZE));
    check((long)ret > 0, "sbrk shrink returned old brk");

    long final = meminfo();
    if (final != baseline) {
        printf("[lazy_sbrk_test] FAIL  meminfo after shrink: final=%ld baseline=%ld\n",
               final, baseline);
        fails++;
    } else {
        printf("[lazy_sbrk_test] PASS  meminfo returned to baseline after shrink\n");
    }

    printf("=== lazy_sbrk_test: %d failures ===\n", fails);
    return fails;
}
