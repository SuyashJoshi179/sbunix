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

    /* Touch each page exactly once; verify free-page count drops by 1
     * each time. The very first fault in a fresh VA region may also
     * allocate intermediate page-table levels (Sv39: L1 + L0 worth of
     * PT pages) — that is layout overhead, not a demand-paging bug.
     * Allow up to 3 extra pages on the first commit. */
    int commit_errors = 0;
    long before_first = meminfo();
    buf[0] = 0;
    long after_first = meminfo();
    long first_delta = before_first - after_first;
    if (first_delta < 1 || first_delta > 4) commit_errors++;
    for (int i = 1; i < NPAGES; i++) {
        long before = meminfo();
        buf[i * PAGE_SIZE] = (char)(i & 0xFF);
        long after = meminfo();
        if (before - after != 1) commit_errors++;
    }
    check(commit_errors == 0, "first-touch commits 1 leaf + up to 3 PT pages");

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
    if (after_unmap - before_unmap < NPAGES) {
        printf("[demand_walk_test] FAIL  unmap delta=%ld want>=%d\n",
               after_unmap - before_unmap, NPAGES);
        fails++;
    } else {
        printf("[demand_walk_test] PASS  munmap released every committed page\n");
    }

    /* Page-table levels allocated on the first fault are kept in the
     * page table after munmap (only the leaf PTEs are cleared). Allow
     * a small residual delta from baseline. */
    if (baseline - after_unmap > 4) {
        printf("[demand_walk_test] FAIL  meminfo %ld vs baseline %ld (delta>4)\n",
               after_unmap, baseline);
        fails++;
    } else {
        printf("[demand_walk_test] PASS  free-page count returns near baseline\n");
    }

    printf("=== demand_walk_test: %d failures ===\n", fails);
    return fails;
}
