#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Grow an allocation across the arena/direct boundary and verify
 * contents survive each realloc. 4 KB → 4 MB → 32 MB. (32 MB rather
 * than spec's 64 MB to leave headroom for other tests under 128 MB
 * physical RAM; still crosses the 16 MB direct-path threshold.) */

int main(void) {
    unsigned long s1 = 4UL * 1024;
    unsigned long s2 = 4UL * 1024 * 1024;
    unsigned long s3 = 32UL * 1024 * 1024;

    char *p = (char *)malloc(s1);
    if (!p) { printf("FAIL malloc %lu\n", s1); return 1; }
    for (unsigned long i = 0; i < s1; i++) p[i] = (char)(i & 0x7F);

    p = (char *)realloc(p, s2);
    if (!p) { printf("FAIL realloc %lu\n", s2); return 1; }
    for (unsigned long i = 0; i < s1; i++) {
        if (p[i] != (char)(i & 0x7F)) {
            printf("FAIL grow1 i=%lu\n", i); return 1;
        }
    }
    for (unsigned long i = s1; i < s2; i++) p[i] = (char)((i >> 4) & 0x7F);

    p = (char *)realloc(p, s3);
    if (!p) { printf("FAIL realloc %lu\n", s3); return 1; }
    for (unsigned long i = 0; i < s1; i++) {
        if (p[i] != (char)(i & 0x7F)) {
            printf("FAIL grow2a i=%lu\n", i); return 1;
        }
    }
    for (unsigned long i = s1; i < s2; i += 4096) {
        if (p[i] != (char)((i >> 4) & 0x7F)) {
            printf("FAIL grow2b i=%lu\n", i); return 1;
        }
    }
    free(p);
    printf("PASS realloc_grow\n");
    return 0;
}
