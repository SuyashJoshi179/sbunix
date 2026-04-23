/*
 * demand_walk_test — allocate a large anonymous mapping and fault each
 * page in individually, verifying:
 *   - each fault commits exactly one physical page;
 *   - pages are independently addressable (write to page i does not bleed
 *     into pages j != i);
 *   - unmapping releases every committed page.
 *
 * Exercises the demand-paging fault handler across many pages, not just
 * the first few that mmap_test already covers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define NPAGES    64

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[demand_walk_test] PASS  %s\n", name);
    } else {
        printf("[demand_walk_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== demand_walk_test ===\n");

    long baseline = meminfo();
    check(baseline > NPAGES + 8, "enough free pages for test");

    volatile char *buf = mmap(0, NPAGES * PAGE_SIZE, PROT_READ | PROT_WRITE,
                              MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)buf > 0, "mmap NPAGES succeeded");
    check(meminfo() == baseline, "mmap did not commit pages (lazy)");

    /* Touch each page exactly once; verify free-page count drops by 1 each time. */
    int commit_errors = 0;
    for (int i = 0; i < NPAGES; i++) {
        long before = meminfo();
        buf[i * PAGE_SIZE] = (char)(i & 0xFF);
        long after = meminfo();
        if (before - after != 1) commit_errors++;
    }
    check(commit_errors == 0, "each first-touch commits exactly one page");

    /* Write a unique pattern to every byte of every page. */
    for (int i = 0; i < NPAGES; i++) {
        memset((void *)(buf + i * PAGE_SIZE), (int)(i & 0xFF), PAGE_SIZE);
    }

    /* Verify no cross-page bleed: every byte of every page still matches. */
    int bleed = 0;
    for (int i = 0; i < NPAGES; i++) {
        for (int j = 0; j < PAGE_SIZE; j += 512) {
            if (buf[i * PAGE_SIZE + j] != (char)(i & 0xFF)) {
                bleed++;
                break;
            }
        }
    }
    check(bleed == 0, "pages are independently addressable (no bleed)");

    long before_unmap = meminfo();
    int rc = munmap((void *)buf, NPAGES * PAGE_SIZE);
    check(rc == 0, "munmap succeeded");

    long after_unmap = meminfo();
    if (after_unmap - before_unmap != NPAGES) {
        printf("[demand_walk_test] FAIL  unmap delta=%ld want=%d\n",
               after_unmap - before_unmap, NPAGES);
        fails++;
    } else {
        printf("[demand_walk_test] PASS  munmap released every committed page\n");
    }

    if (after_unmap != baseline) {
        printf("[demand_walk_test] FAIL  meminfo %ld != baseline %ld\n",
               after_unmap, baseline);
        fails++;
    } else {
        printf("[demand_walk_test] PASS  free-page count returns to baseline\n");
    }

    printf("=== demand_walk_test: %d failures ===\n", fails);
    return fails;
}
