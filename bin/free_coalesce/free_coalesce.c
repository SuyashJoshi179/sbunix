#include <stdio.h>
#include <stdlib.h>

/* Drive the malloc free-list coalesce paths:
 *   - free middle, then left  → coalesce-with-next
 *   - free middle, then right → coalesce-with-prev
 *   - free all three          → coalesce-both
 * After each round, allocate a chunk equal to the combined size and
 * verify malloc reuses the merged block (returns the same base address
 * as the leftmost original chunk). */

#define SZ 4096

static void *grab3[3];

static void grab(void) {
    for (int i = 0; i < 3; i++) {
        grab3[i] = malloc(SZ);
        if (!grab3[i]) { printf("FAIL malloc %d\n", i); exit(1); }
    }
}

int main(void) {
    /* Round 1: free middle then left. */
    grab();
    void *base = grab3[0];
    free(grab3[1]);
    free(grab3[0]);
    free(grab3[2]);
    void *big = malloc(3 * SZ);
    if (big != base) {
        printf("FAIL coalesce R1: big=%p base=%p\n", big, base);
        return 1;
    }
    free(big);

    /* Round 2: free middle then right. */
    grab();
    base = grab3[0];
    free(grab3[1]);
    free(grab3[2]);
    free(grab3[0]);
    big = malloc(3 * SZ);
    if (big != base) {
        printf("FAIL coalesce R2: big=%p base=%p\n", big, base);
        return 1;
    }
    free(big);

    /* Round 3: outer first, middle last (coalesce-both). */
    grab();
    base = grab3[0];
    free(grab3[0]);
    free(grab3[2]);
    free(grab3[1]);
    big = malloc(3 * SZ);
    if (big != base) {
        printf("FAIL coalesce R3: big=%p base=%p\n", big, base);
        return 1;
    }
    free(big);

    printf("PASS free_coalesce\n");
    return 0;
}
