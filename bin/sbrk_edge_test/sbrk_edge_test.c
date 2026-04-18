#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[sbrk_edge_test] PASS  %s\n", name);
    } else {
        printf("[sbrk_edge_test] FAIL  %s\n", name);
        fails++;
    }
}

int main(void) {
    printf("=== sbrk_edge_test ===\n");

    void *base = sbrk(0);
    check((long)base > 0, "sbrk(0) returns valid address");

    void *r = sbrk(4096);
    check(r == base, "sbrk(4096) returns old brk");
    volatile char *p = (volatile char *)base;
    for (int i = 0; i < 4096; i++)
        p[i] = (char)i;
    int data_ok = 1;
    for (int i = 0; i < 4096; i++)
        if (p[i] != (char)i) data_ok = 0;
    check(data_ok, "write+verify full page pattern");

    void *r2 = sbrk(4096 * 4);
    check((long)r2 > 0, "sbrk(4 pages) succeeds");
    volatile char *p2 = (volatile char *)r2;
    p2[0] = 'X';
    p2[4096 * 4 - 1] = 'Y';
    check(p2[0] == 'X' && p2[4096 * 4 - 1] == 'Y', "multi-page heap r/w");

    check(p[0] == (char)0, "first page still intact after grow");

    void *before = sbrk(0);
    void *sr = sbrk(-4096 * 4);
    check(sr == before, "sbrk(-4pages) returns old brk");
    void *after = sbrk(0);
    check((char *)after == (char *)before - 4096 * 4, "brk shrank correctly");

    check(p[0] == (char)0, "first page still intact after shrink");

    void *re = sbrk(4096);
    check((long)re > 0, "re-grow after shrink succeeds");
    volatile char *p3 = (volatile char *)re;
    p3[0] = 'R';
    check(p3[0] == 'R', "re-grown page is writable");

    sbrk(-4096);

    int pid = fork();
    if (pid == 0) {
        void *child_brk = sbrk(0);
        check(child_brk == sbrk(0), "child: sbrk(0) consistent");
        void *cr = sbrk(4096);
        check((long)cr > 0, "child: sbrk(4096) succeeds");
        volatile char *cp = (volatile char *)cr;
        cp[0] = 'C';
        check(cp[0] == 'C', "child: heap write succeeds");
        exit(0);
    }
    int st;
    wait(&st);
    check(st == 0, "child exited cleanly");

    void *parent_brk = sbrk(0);
    check((char *)parent_brk == (char *)base + 4096, "parent brk unchanged after child sbrk");

    int pid2 = fork();
    if (pid2 == 0) {
        *(volatile char *)0 = 0;
        exit(99);
    }
    wait(&st);
    check(st != 0, "child touching freed memory killed");

    printf("=== sbrk_edge_test: %d failures ===\n", fails);
    return fails;
}
