#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned long below = 8UL * 1024 * 1024;
    unsigned long above = 32UL * 1024 * 1024;
    char *a = (char *)malloc(below);
    char *b = (char *)malloc(above);
    if (!a || !b) { printf("FAIL alloc\n"); return 1; }

    unsigned long ua = (unsigned long)a;
    unsigned long ub = (unsigned long)b;
    if (ua < (1UL << 32) || ua >= (1UL << 36)) {
        printf("FAIL below-threshold ptr outside heap arena: %p\n", a);
        return 1;
    }
    if (ub < (1UL << 36)) {
        printf("FAIL above-threshold ptr inside heap arena: %p\n", b);
        return 1;
    }
    free(a); free(b);
    printf("PASS direct_threshold\n");
    return 0;
}
