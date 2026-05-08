#include <stdio.h>
#include <stdlib.h>

/* Allocate many small chunks contiguously, free all in reverse
 * (forces stepwise coalesce-with-next), then ask for one large
 * arena-path chunk equal to the freed total. The free-list must
 * have merged into a single span big enough to satisfy it without
 * bumping arena_top. We can't observe arena_top from userspace,
 * but if coalescing failed the large request would force a bump
 * past the prior frontier — so we settle for verifying success. */

#define N    256
#define SZ   4096

static void *small[N];

int main(void) {
    for (int i = 0; i < N; i++) {
        small[i] = malloc(SZ);
        if (!small[i]) { printf("FAIL alloc %d\n", i); return 1; }
    }
    void *base = small[0];
    for (int i = N - 1; i >= 0; i--) free(small[i]);

    void *big = malloc(N * SZ);
    if (!big) { printf("FAIL big malloc\n"); return 1; }
    if (big != base) {
        printf("FAIL coalesce: big=%p base=%p\n", big, base);
        return 1;
    }
    free(big);
    printf("PASS free_then_huge\n");
    return 0;
}
