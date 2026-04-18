#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[mmap_stress_test] PASS  %s\n", name);
    } else {
        printf("[mmap_stress_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== mmap_stress_test ===\n");

    void *addrs[32];
    int alloc_ok = 1;
    for (int i = 0; i < 32; i++) {
        addrs[i] = mmap(0, 4096, PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
        if ((long)addrs[i] <= 0) {
            alloc_ok = 0;
            addrs[i] = 0;
        }
    }
    check(alloc_ok, "32 mmap allocations succeeded");

    int unique = 1;
    for (int i = 0; i < 32; i++) {
        for (int j = i + 1; j < 32; j++) {
            if (addrs[i] && addrs[j] && addrs[i] == addrs[j])
                unique = 0;
        }
    }
    check(unique, "all 32 addresses are distinct");

    int rw_ok = 1;
    for (int i = 0; i < 32; i++) {
        if (!addrs[i]) continue;
        volatile char *p = (volatile char *)addrs[i];
        p[0] = (char)(i + 1);
        p[4095] = (char)(i + 100);
        if (p[0] != (char)(i + 1) || p[4095] != (char)(i + 100))
            rw_ok = 0;
    }
    check(rw_ok, "write+read all 32 regions");

    int unmap_ok = 1;
    for (int i = 0; i < 32; i++) {
        if (!addrs[i]) continue;
        if (munmap(addrs[i], 4096) != 0)
            unmap_ok = 0;
    }
    check(unmap_ok, "munmap all 32 regions");

    void *after[8];
    int reuse_ok = 1;
    for (int i = 0; i < 8; i++) {
        after[i] = mmap(0, 4096, PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
        if ((long)after[i] <= 0) reuse_ok = 0;
    }
    check(reuse_ok, "re-mmap after full munmap succeeds");
    for (int i = 0; i < 8; i++) {
        if ((long)after[i] > 0) munmap(after[i], 4096);
    }

    void *big = mmap(0, 16 * 4096, PROT_READ | PROT_WRITE,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
    check((long)big > 0, "multi-page mmap (64KB)");
    if ((long)big > 0) {
        volatile char *bp = (volatile char *)big;
        bp[0] = 'A';
        bp[16 * 4096 - 1] = 'Z';
        check(bp[0] == 'A' && bp[16 * 4096 - 1] == 'Z', "multi-page r/w");
        check(munmap(big, 16 * 4096) == 0, "multi-page munmap");
    }

    printf("=== mmap_stress_test: %d failures ===\n", fails);
    return fails;
}
