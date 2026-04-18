#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int segv_seen;

static void on_segv(int sig) {
    (void)sig;
    segv_seen = 1;
    exit(0);
}

int main(void) {
    int pid = fork();
    if (pid < 0) {
        printf("sigsegv_handler_test: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        struct sigaction sa;
        sa.sa_handler = on_segv;
        sa.sa_mask = 0;
        sa.sa_flags = 0;
        sa.sa_restorer = 0;
        if (sigaction(SIGSEGV, &sa, 0) < 0)
            exit(2);

        volatile int *p = (int *)0;
        *p = 1;
        exit(segv_seen ? 0 : 3);
    }

    int st = -1;
    wait(&st);
    if (st != 0) {
        printf("sigsegv_handler_test: handled case failed status=%d\n", st);
        return 1;
    }

    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        volatile int *p = (int *)0;
        *p = 1;
        exit(9);
    }
    wait(&st);
    if (st != (128 + SIGSEGV)) {
        printf("sigsegv_handler_test: default case status=%d want=%d\n",
               st, 128 + SIGSEGV);
        return 1;
    }

    printf("sigsegv_handler_test: PASS\n");
    return 0;
}
