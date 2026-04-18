#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[malloc_test] PASS  %s\n", name);
    } else {
        printf("[malloc_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== malloc_test ===\n");

    void *p = malloc(64);
    check(p != 0, "malloc(64) returns non-null");

    memset(p, 0xAB, 64);
    unsigned char *b = (unsigned char *)p;
    check(b[0] == 0xAB && b[63] == 0xAB, "write+read 64 bytes");
    free(p);

    void *c = calloc(10, 32);
    check(c != 0, "calloc(10,32) returns non-null");
    b = (unsigned char *)c;
    int all_zero = 1;
    for (int i = 0; i < 320; i++) {
        if (b[i] != 0) { all_zero = 0; break; }
    }
    check(all_zero, "calloc memory is zeroed");
    free(c);

    void *ptrs[50];
    int alloc_ok = 1;
    for (int i = 0; i < 50; i++) {
        unsigned long sz = 16 + (unsigned long)(i * 20);
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) { alloc_ok = 0; break; }
        memset(ptrs[i], (unsigned char)(i & 0xFF), sz);
    }
    check(alloc_ok, "50 mallocs of varying size");

    int pattern_ok = 1;
    for (int i = 0; i < 50; i++) {
        unsigned long sz = 16 + (unsigned long)(i * 20);
        unsigned char *bp = (unsigned char *)ptrs[i];
        unsigned char expected = (unsigned char)(i & 0xFF);
        for (unsigned long j = 0; j < sz; j++) {
            if (bp[j] != expected) { pattern_ok = 0; break; }
        }
        if (!pattern_ok) break;
    }
    check(pattern_ok, "all patterns intact");

    for (int i = 0; i < 50; i += 2)
        free(ptrs[i]);
    for (int i = 1; i < 50; i += 2)
        free(ptrs[i]);

    void *reuse = malloc(128);
    check(reuse != 0, "malloc after free-all returns non-null");
    free(reuse);

    void *r = malloc(32);
    check(r != 0, "realloc: initial malloc");
    memset(r, 'X', 32);
    r = realloc(r, 256);
    check(r != 0, "realloc to 256 returns non-null");
    b = (unsigned char *)r;
    check(b[0] == 'X' && b[31] == 'X', "realloc preserves old data");
    free(r);

    check(malloc(0) == 0, "malloc(0) returns null");
    free((void *)0);
    check(1, "free(null) does not crash");

    printf("=== malloc_test: %d failures ===\n", fails);
    return fails;
}
