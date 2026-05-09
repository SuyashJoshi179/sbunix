/*
 * grader_malloc_test — Professor-grade memory allocator test.
 *
 * Tests progressive sizes (64KB → 100MB), lazy allocation, calloc zeroing,
 * realloc across page boundaries, fork-cycle stability, and large
 * allocation under memory pressure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_malloc_test] PASS  %s\n", name);
    } else {
        printf("[grader_malloc_test] FAIL  %s\n", name);
        fails++;
    }
}

/* Helper: fork a child that mallocs `size` bytes, fills with `val`,
 * verifies, frees, and exits. Returns child exit status. */
static int fork_alloc_child(unsigned long size, unsigned char val) {
    int pid = fork();
    if (pid == 0) {
        unsigned char *p = (unsigned char *)malloc(size);
        if (!p) exit(1);
        memset(p, val, size);
        /* Spot-check */
        int ok = (p[0] == val && p[size/2] == val && p[size-1] == val);
        free(p);
        exit(ok ? 0 : 2);
    }
    int st = -1;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void) {
    printf("=== grader_malloc_test ===\n");

    /* 1. Progressive sizes — all must succeed */
    void *p64k = malloc(64 * 1024);
    check(p64k != 0, "malloc(64KB) succeeds");
    if (p64k) { memset(p64k, 'A', 64 * 1024); free(p64k); }

    void *p256k = malloc(256 * 1024);
    check(p256k != 0, "malloc(256KB) succeeds");
    if (p256k) { memset(p256k, 'B', 256 * 1024); free(p256k); }

    void *p1m = malloc(1024 * 1024);
    check(p1m != 0, "malloc(1MB) succeeds");
    if (p1m) {
        /* Touch every page to force physical allocation */
        volatile char *v = (volatile char *)p1m;
        for (unsigned long i = 0; i < 1024 * 1024; i += 4096)
            v[i] = (char)(i & 0xFF);
        free(p1m);
    }

    void *p4m = malloc(4 * 1024 * 1024);
    check(p4m != 0, "malloc(4MB) succeeds");
    if (p4m) {
        volatile char *v = (volatile char *)p4m;
        v[0] = 'X';
        v[4 * 1024 * 1024 - 1] = 'Y';
        free(p4m);
    }

    /* 2. Enormous lazy allocation — professor tests this explicitly.
     * Must succeed (return non-NULL). Only touch first + last page. */
    void *p64m = malloc(64 * 1024 * 1024);
    check(p64m != 0, "malloc(64MB) succeeds (lazy)");
    if (p64m) {
        volatile char *v = (volatile char *)p64m;
        v[0] = 'L';
        v[64 * 1024 * 1024 - 1] = 'Z';
        check(v[0] == 'L' && v[64 * 1024 * 1024 - 1] == 'Z',
              "malloc(64MB) first+last page writable");
        free(p64m);
    }

    /* 2b. 400MB allocation — don't write to it at all.
     * This is the pure lazy allocation test: sbrk extends the VMA
     * but zero physical pages should be committed. Must not fail.
     * (We use 400MB because earlier tests consumed ~70MB of the 512MB heap). */
    {
        void *p400m = malloc(400UL * 1024 * 1024);
        check(p400m != 0, "malloc(400MB) succeeds (pure lazy, no touch)");
        if (p400m) free(p400m);
    }

    /* 2c. 128MB allocation — touch only 2 pages out of 32768.
     * Verifies the OS doesn't eagerly commit all pages. */
    {
        void *p128m = malloc(128UL * 1024 * 1024);
        check(p128m != 0, "malloc(128MB) succeeds (lazy)");
        if (p128m) {
            volatile char *v = (volatile char *)p128m;
            v[0] = 'A';
            v[128UL * 1024 * 1024 - 1] = 'Z';
            check(v[0] == 'A' && v[128UL * 1024 * 1024 - 1] == 'Z',
                  "malloc(128MB) first+last page writable");
            free(p128m);
        }
    }

    /* 3. calloc — must be zeroed */
    unsigned char *cz = (unsigned char *)calloc(1024, 1024);
    check(cz != 0, "calloc(1MB) succeeds");
    if (cz) {
        int all_zero = 1;
        for (unsigned long i = 0; i < 1024 * 1024; i += 4096) {
            if (cz[i] != 0) { all_zero = 0; break; }
        }
        check(all_zero, "calloc(1MB) is zeroed");
        free(cz);
    }

    /* 4. realloc across page boundary */
    void *r = malloc(64);
    check(r != 0, "realloc: initial malloc(64)");
    if (r) {
        memset(r, 'R', 64);
        r = realloc(r, 256 * 1024);
        check(r != 0, "realloc(256KB) succeeds");
        if (r) {
            unsigned char *rb = (unsigned char *)r;
            check(rb[0] == 'R' && rb[63] == 'R',
                  "realloc preserves original 64 bytes");
            free(r);
        }
    }

    /* 5. malloc(0) and free(NULL) edge cases */
    check(malloc(0) == 0, "malloc(0) returns NULL");
    free(0);
    check(1, "free(NULL) does not crash");

    /* 6. Fork-cycle stability (Eval 07): fork 20 children sequentially,
     * each allocs + writes + exits. No cascading failures. */
    {
        int cycle_ok = 1;
        for (int i = 0; i < 20; i++) {
            int st = fork_alloc_child(64 * 1024, (unsigned char)(i & 0xFF));
            if (st != 0) { cycle_ok = 0; break; }
        }
        check(cycle_ok, "fork-cycle: 20 sequential fork+malloc+exit children");
    }

    /* 7. Large allocation under memory pressure (Eval 10):
     * Fork child that allocs 30 MB, writes all, exits cleanly. */
    {
        int st30 = fork_alloc_child(30UL * 1024 * 1024, 0xAA);
        check(st30 == 0, "pressure: child malloc(30MB)+write+exit succeeds");
    }

    printf("=== grader_malloc_test: %d failures ===\n", fails);
    return fails;
}
