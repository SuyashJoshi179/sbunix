#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned long sz = 4UL * 1024 * 1024 * 1024;
    char *p = (char *)malloc(sz);
    if (!p) { printf("FAIL malloc 4GiB returned NULL\n"); return 1; }
    p[0] = 'A';
    p[sz - 1] = 'Z';
    if (p[0] != 'A' || p[sz - 1] != 'Z') {
        printf("FAIL pattern mismatch %c %c\n", p[0], p[sz - 1]);
        return 1;
    }
    free(p);
    printf("PASS lazy_reserve 4GiB\n");
    return 0;
}
