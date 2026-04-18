#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int saw_pipe;

static void on_pipe(int sig) {
    (void)sig;
    saw_pipe = 1;
}

int main(void) {
    int fds[2];
    if (pipe(fds) < 0) {
        printf("sigpipe_test: pipe failed\n");
        return 1;
    }

    int pid = fork();
    if (pid < 0) {
        printf("sigpipe_test: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        close(fds[0]);
        close(fds[1]);

        struct sigaction sa;
        sa.sa_handler = on_pipe;
        sa.sa_mask = 0;
        sa.sa_flags = 0;
        sa.sa_restorer = 0;
        if (sigaction(SIGPIPE, &sa, 0) < 0)
            return 2;

        int pf[2];
        if (pipe(pf) < 0)
            return 3;
        close(pf[0]);

        long w = write(pf[1], "x", 1);
        if (w != -EPIPE)
            return 4;
        if (!saw_pipe)
            return 5;
        return 0;
    }

    close(fds[0]);
    close(fds[1]);

    int st = -1;
    wait(&st);
    if (st != 0) {
        printf("sigpipe_test: child failed status=%d\n", st);
        return 1;
    }

    printf("sigpipe_test: PASS\n");
    return 0;
}
