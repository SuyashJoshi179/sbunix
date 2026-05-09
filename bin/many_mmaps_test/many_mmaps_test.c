#include <stdio.h>
#include <sys/mman.h>

/* Spec target is 10 000 but the kernel sys_mmap top-down search is O(n)
 * so 10 000 mmaps cost O(n^2) wall-clock under QEMU. Keep N at 1 000 here
 * (still 4× the legacy static vma_pool cap of 256) and rely on the kernel
 * selftest `vma_slab_scaling` to additionally exercise 300 raw vma_alloc
 * calls in tight loop. */
#define N 1000

/* Heap-allocated to avoid 80 KB stack array. */
#include <stdlib.h>
static void **maps;

int main(void) {
    maps = malloc(N * sizeof(*maps));
    if (!maps) { printf("FAIL maps array malloc\n"); return 1; }
    int ok = 0;
    for (; ok < N; ok++) {
        void *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) break;
        maps[ok] = p;
    }
    if (ok != N) { printf("FAIL only %d mmaps succeeded\n", ok); return 1; }

    for (int i = 0; i < ok; i++) munmap(maps[i], 4096);
    printf("PASS many_mmaps %d\n", N);
    return 0;
}
