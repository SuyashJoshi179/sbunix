#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[sbrk_test] PASS  %s\n", name);
    } else {
        printf("[sbrk_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== sbrk_test ===\n");

    void *base = sbrk(0);
    check((long)base > 0x1000, "sbrk(0) returns address above text");

    void *old = sbrk(4096);
    check(old == base, "sbrk(4096) returns old brk");

    void *cur = sbrk(0);
    check((char *)cur == (char *)base + 4096, "brk advanced by 4096");

    volatile char *p = (char *)base;
    p[0] = 'A';
    p[4095] = 'Z';
    check(p[0] == 'A' && p[4095] == 'Z', "write+read heap page");

    void *old2 = sbrk(4096);
    check(old2 == cur, "second sbrk(4096) returns old brk");
    volatile char *p2 = (char *)old2;
    p2[0] = 'B';
    check(p2[0] == 'B', "write+read second heap page");
    check(p[0] == 'A', "first page still intact");

    void *before_shrink = sbrk(0);
    void *ret = sbrk(-4096);
    check(ret == before_shrink, "sbrk(-4096) returns old brk");
    void *after_shrink = sbrk(0);
    check((char *)after_shrink == (char *)before_shrink - 4096, "brk shrunk by 4096");

    printf("=== sbrk_test: %d failures ===\n", fails);
    return fails;
}
