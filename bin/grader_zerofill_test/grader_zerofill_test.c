/*
 * grader_zerofill_test — Recycled page zero-fill.
 *
 * POSIX requires that freshly allocated pages (heap, stack, mmap) contain
 * zeroes, even if the physical page was previously used by another process.
 * (Based on Eval 08 and Eval 09.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_zerofill_test] PASS  %s\n", name);
    } else {
        printf("[grader_zerofill_test] FAIL  %s\n", name);
        fails++;
    }
}

/* Fork a child that dirties `npages` worth of heap memory, then exits. */
static void dirty_pages(int npages) {
    int pid = fork();
    if (pid == 0) {
        unsigned long sz = (unsigned long)npages * 4096;
        volatile unsigned char *p = (volatile unsigned char *)malloc(sz);
        if (p) {
            for (unsigned long i = 0; i < sz; i += 4096)
                p[i] = 0xDE;
            free((void *)p);
        }
        exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
}

int main(void) {
    printf("=== grader_zerofill_test ===\n");

    /* 1. sbrk fresh pages must be zeroed */
    void *brk0 = sbrk(0);
    void *r = sbrk(4096);
    check((long)r > 0, "sbrk(4096) succeeds");
    if ((long)r > 0) {
        volatile unsigned char *p = (volatile unsigned char *)r;
        int all_zero = 1;
        for (int i = 0; i < 4096; i++) {
            if (p[i] != 0) { all_zero = 0; break; }
        }
        check(all_zero, "sbrk page is zero-filled");
        sbrk(-4096);
    }

    /* 2. mmap anonymous pages must be zeroed */
    void *m = mmap(0, 4 * 4096, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_PRIVATE, -1, 0);
    check(m != MAP_FAILED && (long)m > 0, "mmap(4 pages anon) succeeds");
    if (m != MAP_FAILED && (long)m > 0) {
        volatile unsigned char *p = (volatile unsigned char *)m;
        int all_zero = 1;
        for (int i = 0; i < 4 * 4096; i++) {
            if (p[i] != 0) { all_zero = 0; break; }
        }
        check(all_zero, "mmap anonymous pages are zero-filled");
        munmap(m, 4 * 4096);
    }

    /* 3. Dirty pages, then verify fresh allocs are still zeroed.
     * This catches the recycled-page bug from Eval 08. */
    for (int round = 0; round < 5; round++)
        dirty_pages(16);

    /* After dirtying, fresh sbrk should still be zeroed */
    r = sbrk(8192);
    check((long)r > 0, "sbrk(8K) after dirty cycle succeeds");
    if ((long)r > 0) {
        volatile unsigned char *p = (volatile unsigned char *)r;
        int all_zero = 1;
        for (int i = 0; i < 8192; i++) {
            if (p[i] != 0) { all_zero = 0; break; }
        }
        check(all_zero, "sbrk pages zeroed after dirty cycle");
        sbrk(-8192);
    }

    /* After dirtying, fresh mmap should still be zeroed */
    m = mmap(0, 4096, PROT_READ | PROT_WRITE,
             MAP_ANON | MAP_PRIVATE, -1, 0);
    check(m != MAP_FAILED && (long)m > 0,
          "mmap after dirty cycle succeeds");
    if (m != MAP_FAILED && (long)m > 0) {
        volatile unsigned char *p = (volatile unsigned char *)m;
        int all_zero = 1;
        for (int i = 0; i < 4096; i++) {
            if (p[i] != 0) { all_zero = 0; break; }
        }
        check(all_zero, "mmap page zeroed after dirty cycle");
        munmap(m, 4096);
    }

    /* 4. Eval 08 exact scenario: dirty child, then fork+exec a program.
     * The exec'd program's stack must be zeroed or it silently fails. */
    dirty_pages(64);  /* dirty 64 pages */
    {
        int pid = fork();
        if (pid == 0) {
            /* exec a simple program — if its stack is corrupted by
             * stale page data, it will crash or produce no output. */
            char *args[] = {"/bin/echo", "zerofill_exec_ok", 0};
            execv("/bin/echo", args);
            /* If exec fails, exit with error */
            exit(99);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "exec after dirty cycle: program runs correctly");
    }

    printf("=== grader_zerofill_test: %d failures ===\n", fails);
    return fails;
}
