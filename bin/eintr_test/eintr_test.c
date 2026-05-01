#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile int got_term;

static void on_term(int sig) {
    (void)sig;
    got_term = 1;
}

int main(void) {
    int pf[2];
    if (pipe(pf) < 0) {
        printf("eintr_test: pipe failed\n");
        return 1;
    }

    int pid = fork();
    if (pid < 0) {
        printf("eintr_test: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        struct sigaction sa;
        sa.sa_handler = on_term;
        sa.sa_mask = 0;
        sa.sa_flags = 0;
        sa.sa_restorer = 0;
        if (sigaction(SIGTERM, &sa, 0) < 0)
            exit(2);

        close(pf[1]);

        char c;
        long r = read(pf[0], &c, 1);
        if (r != -EINTR)
            exit(3);
        if (!got_term)
            exit(4);

        struct timespec req = { .tv_sec = 2, .tv_nsec = 0 };
        struct timespec rem = {0, 0};
        got_term = 0;
        r = nanosleep(&req, &rem);
        if (r != -EINTR)
            exit(5);
        if (!got_term)
            exit(6);
        if (rem.tv_sec <= 0)
            exit(7);

        exit(42);
    }

    close(pf[0]);

    sleep_ms(200);
    kill(pid, SIGTERM);
    sleep_ms(200);
    kill(pid, SIGTERM);

    int st = -1;
    wait(&st);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 42) {
        printf("eintr_test: child status=%d want exit(42)\n", st);
        return 1;
    }

    printf("eintr_test: PASS\n");
    return 0;
}
