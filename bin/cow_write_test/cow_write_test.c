#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[cow_write_test] PASS  %s\n", name);
    } else {
        printf("[cow_write_test] FAIL  %s\n", name);
        fails++;
    }
}

static volatile int g1 = 100;
static volatile int g2 = 200;
static volatile int g3 = 300;
static volatile int array[256];

int main(void) {
    printf("=== cow_write_test ===\n");

    for (int i = 0; i < 256; i++)
        array[i] = i;

    int pid1 = fork();
    if (pid1 == 0) {
        g1 = 999;
        g2 = 888;
        g3 = 777;
        for (int i = 0; i < 256; i++)
            array[i] = 1000 + i;
        check(g1 == 999, "child1: g1 written");
        check(array[0] == 1000, "child1: array[0] written");
        check(array[255] == 1255, "child1: array[255] written");
        exit(0);
    }

    int pid2 = fork();
    if (pid2 == 0) {
        check(g1 == 100, "child2: sees original g1");
        check(g2 == 200, "child2: sees original g2");
        check(array[0] == 0, "child2: sees original array[0]");
        g1 = 555;
        g2 = 444;
        check(g1 == 555, "child2: g1 written independently");
        exit(0);
    }

    int st;
    wait(&st);
    check(st == 0, "child1 exited cleanly");
    wait(&st);
    check(st == 0, "child2 exited cleanly");

    check(g1 == 100, "parent: g1 unchanged after both children");
    check(g2 == 200, "parent: g2 unchanged");
    check(g3 == 300, "parent: g3 unchanged");
    check(array[0] == 0, "parent: array[0] unchanged");
    check(array[255] == 255, "parent: array[255] unchanged");

    int pid3 = fork();
    if (pid3 == 0) {
        g1 = 42;
        int pid4 = fork();
        if (pid4 == 0) {
            check(g1 == 42, "grandchild: sees child's write");
            g1 = 84;
            check(g1 == 84, "grandchild: wrote own copy");
            exit(0);
        }
        wait(&st);
        check(st == 0, "grandchild exited cleanly");
        check(g1 == 42, "child3: unchanged after grandchild write");
        exit(0);
    }
    wait(&st);
    check(st == 0, "child3 (nested fork) exited cleanly");
    check(g1 == 100, "parent: still original after nested fork");

    printf("=== cow_write_test: %d failures ===\n", fails);
    return fails;
}
