#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile int chld_seen;

static void on_chld(int sig) {
    (void)sig;
    chld_seen = 1;
}

int main(void) {
    struct sigaction sa;
    sa.sa_handler = on_chld;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;

    if (sigaction(SIGCHLD, &sa, 0) < 0) {
        printf("sigchld_test: sigaction failed\n");
        return 1;
    }

    int pid = fork();
    if (pid < 0) {
        printf("sigchld_test: fork failed\n");
        return 1;
    }
    if (pid == 0)
        return 7;

    int st = -1;
    int got = -1;
    for (;;) {
        got = wait(&st);
        if (got >= 0) break;
    }

    if (!chld_seen) {
        printf("sigchld_test: handler did not run\n");
        return 1;
    }
    if (got != pid) {
        printf("sigchld_test: wait pid mismatch got=%d want=%d\n", got, pid);
        return 1;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 7) {
        printf("sigchld_test: status mismatch got=%d want exit(7)\n", st);
        return 1;
    }

    printf("sigchld_test: PASS\n");
    return 0;
}
