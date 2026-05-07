#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>

#define MMAP_BASE_HINT  0x0000001000000000UL  /* 64 GiB */

int main(void) {
    int pass = 0, fail = 0;

    void *p = mmap((void *)MMAP_BASE_HINT, 4096,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED || p != (void *)MMAP_BASE_HINT) {
        printf("FAIL fixed at 64GiB (got %p errno=%d)\n", p, errno); fail++;
    } else { pass++; printf("PASS fixed at 64GiB\n"); }

    void *p2 = mmap((void *)MMAP_BASE_HINT, 4096,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p2 != MAP_FAILED || errno != EINVAL) {
        printf("FAIL overlap rejection (got %p errno=%d)\n", p2, errno); fail++;
    } else { pass++; printf("PASS overlap rejection\n"); }

    void *p3 = mmap((void *)0x100, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p3 != MAP_FAILED || errno != EINVAL) {
        printf("FAIL below text rejection\n"); fail++;
    } else { pass++; printf("PASS below text rejection\n"); }

    if (p != MAP_FAILED) munmap(p, 4096);
    printf("mmap_fixed_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
