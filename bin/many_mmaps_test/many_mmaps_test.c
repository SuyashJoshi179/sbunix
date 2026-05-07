#include <stdio.h>
#include <sys/mman.h>

#define N 1000

static void *maps[N];

int main(void) {
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
