#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int spawn_once(void) {
    int pid = fork();
    if (pid < 0) return pid;
    if (pid == 0) {
        int sink[2];
        if (pipe(sink) == 0) {
            close(1);
            dup(sink[1]);
            close(2);
            dup(sink[1]);
            close(sink[1]);
            // Keep sink[0] open in this short-lived process so writes
            // do not hit SIGPIPE and never reach the console.
        }
        char *argv[] = {"echo", 0};
        execv("/bin/echo", argv);
        exit(127);
    }

    int st = 0;
    while (1) {
        int got = wait(&st);
        if (got == pid) break;
        if (got == -EINTR) continue;
        if (got < 0) return got;
    }
    return st == 0 ? 0 : -1;
}

int main(void) {
    long free_before = meminfo();
    if (free_before <= 0) {
        printf("leak_test: meminfo before invalid: %ld\n", free_before);
        return 1;
    }

    for (int i = 0; i < 1000; i++) {
        int r = spawn_once();
        if (r < 0) {
            printf("leak_test: iteration %d failed (%d)\n", i, r);
            return 1;
        }
    }

    long free_after = meminfo();
    if (free_after <= 0) {
        printf("leak_test: meminfo after invalid: %ld\n", free_after);
        return 1;
    }

    if (free_before != free_after) {
        printf("leak_test: free_before=%ld free_after=%ld\n", free_before, free_after);
        return 1;
    }

    printf("leak_test: PASS\n");
    return 0;
}
