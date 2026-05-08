#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1000 mallocs of varied sizes, each filled with a unique pattern
 * derived from the allocation index. Verify after all allocs are done
 * that no chunk's bytes were stomped by a neighbor. */

#define N 1000

struct slot { unsigned char *p; unsigned long sz; unsigned char tag; };

static struct slot slots[N];

int main(void) {
    for (int i = 0; i < N; i++) {
        unsigned long sz = 16 + (unsigned long)((i * 73) % 4096);
        unsigned char *p = (unsigned char *)malloc(sz);
        if (!p) { printf("FAIL malloc i=%d\n", i); return 1; }
        unsigned char tag = (unsigned char)(i & 0xFF);
        memset(p, tag, sz);
        slots[i].p   = p;
        slots[i].sz  = sz;
        slots[i].tag = tag;
    }
    for (int i = 0; i < N; i++) {
        for (unsigned long j = 0; j < slots[i].sz; j++) {
            if (slots[i].p[j] != slots[i].tag) {
                printf("FAIL bleed i=%d j=%lu got=%u want=%u\n",
                       i, j, slots[i].p[j], slots[i].tag);
                return 1;
            }
        }
    }
    for (int i = 0; i < N; i++) free(slots[i].p);
    printf("PASS multi_alloc_disjoint %d chunks\n", N);
    return 0;
}
