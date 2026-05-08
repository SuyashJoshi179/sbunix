#include <stdio.h>
#include <stdlib.h>

/* Verify calloc returns zero bytes both for an arena-served chunk
 * (small) and for a direct-mmap chunk (>= 16 MB threshold). */

static int verify_zero(const char *p, unsigned long sz, const char *name) {
    for (unsigned long i = 0; i < sz; i += 4096) {
        if (p[i] != 0) { printf("FAIL %s nonzero at %lu\n", name, i); return 1; }
    }
    return 0;
}

int main(void) {
    char *small = (char *)calloc(1, 1UL << 20);   /* 1 MB arena path */
    if (!small) { printf("FAIL calloc small\n"); return 1; }
    if (verify_zero(small, 1UL << 20, "small")) return 1;
    free(small);

    char *big = (char *)calloc(1, 32UL * 1024 * 1024); /* 32 MB direct path */
    if (!big) { printf("FAIL calloc big\n"); return 1; }
    if (verify_zero(big, 32UL * 1024 * 1024, "big")) return 1;
    free(big);

    printf("PASS calloc_zero\n");
    return 0;
}
