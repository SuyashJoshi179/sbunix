#include <stdio.h>
#include <stdlib.h>

int main(void) {
    void *a = malloc(4096);
    if (!a) { printf("FAIL alloc1\n"); return 1; }
    free(a);
    void *b = malloc(4096);
    if (!b) { printf("FAIL alloc2\n"); return 1; }
    if (a != b) {
        printf("FAIL freelist did not reuse: %p vs %p\n", a, b);
        return 1;
    }
    free(b);
    printf("PASS free_reuse\n");
    return 0;
}
