#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[fork_storm_test] PASS  %s\n", name);
    } else {
        printf("[fork_storm_test] FAIL  %s\n", name);
        fails++;
    }
}

#define NCHILDREN 16

static volatile int marker = 0xABCD;

int main(void) {
    printf("=== fork_storm_test ===\n");

    int pids[NCHILDREN];
    int fork_ok = 1;

    for (int i = 0; i < NCHILDREN; i++) {
        int pid = fork();
        if (pid == 0) {
            volatile int local = i * 1000;
            for (int j = 0; j < 100; j++)
                local += 1;
            check(local == i * 1000 + 100, "child compute correct");
            check(marker == 0xABCD, "child sees original marker");
            marker = i;
            check(marker == i, "child wrote marker");
            exit(0);
        }
        if (pid < 0) {
            fork_ok = 0;
            pids[i] = -1;
        } else {
            pids[i] = pid;
        }
    }

    check(fork_ok, "all 16 forks succeeded");

    int all_clean = 1;
    for (int i = 0; i < NCHILDREN; i++) {
        if (pids[i] < 0) continue;
        int st;
        wait(&st);
        if (st != 0) all_clean = 0;
    }
    check(all_clean, "all 16 children exited with status 0");
    check(marker == 0xABCD, "parent marker unchanged after 16 children");

    printf("=== fork_storm_test: %d failures ===\n", fails);
    return fails;
}
