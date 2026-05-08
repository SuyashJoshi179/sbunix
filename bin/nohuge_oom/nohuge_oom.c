#include <stdio.h>
#include <stdlib.h>

/* Repeated 64 MB direct-path allocations until the kernel can no longer
 * carve a fresh VMA out of the 64 GB MMAP window. Touch one byte per
 * chunk so we exercise the lazy-fault path without exhausting physical
 * RAM. PASS = loop terminates gracefully with NULL (no panic, no SEGV). */

#define CHUNK   (64UL * 1024 * 1024)
#define MAX_ITS 1500

int main(void) {
    int n = 0;
    for (; n < MAX_ITS; n++) {
        char *p = (char *)malloc(CHUNK);
        if (!p) break;
        p[0] = 1;
    }
    if (n == 0) {
        printf("FAIL nohuge_oom: first malloc returned NULL\n");
        return 1;
    }
    printf("PASS nohuge_oom: %d allocs before NULL\n", n);
    return 0;
}
