#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned long sz = 32UL * 1024 * 1024 * 1024;
    char *p = (char *)malloc(sz);
    if (!p) { printf("FAIL malloc 32GiB returned NULL\n"); return 1; }
    free(p);
    printf("PASS huge_reserve_no_touch 32GiB\n");
    return 0;
}
