#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int run_normal_exit(int code) {
    int pid = fork();
    if (pid == 0) {
        exit(code);
    }
    int s = 0;
    int got = wait(&s);
    if (got != pid) {
        printf("wait_test: wait got=%d pid=%d\n", got, pid);
        return 1;
    }
    if (!WIFEXITED(s) || WIFSIGNALED(s) || WEXITSTATUS(s) != code) {
        printf("wait_test: normal exit decode wrong: s=%d exited=%d signaled=%d code=%d\n",
               s, WIFEXITED(s), WIFSIGNALED(s), WEXITSTATUS(s));
        return 1;
    }
    return 0;
}

static int run_signal_exit(int sig) {
    int pid = fork();
    if (pid == 0) {
        raise(sig);
        for (;;) sched_yield();
    }
    int s = 0;
    int got = wait(&s);
    if (got != pid) {
        printf("wait_test: wait got=%d pid=%d\n", got, pid);
        return 1;
    }
    if (!WIFSIGNALED(s) || WIFEXITED(s) || WTERMSIG(s) != sig) {
        printf("wait_test: signal exit decode wrong: s=%d exited=%d signaled=%d sig=%d (want %d)\n",
               s, WIFEXITED(s), WIFSIGNALED(s), WTERMSIG(s), sig);
        return 1;
    }
    return 0;
}

int main(void) {
    if (run_normal_exit(0))    return 1;
    if (run_normal_exit(7))    return 1;
    if (run_normal_exit(42))   return 1;
    if (run_signal_exit(SIGTERM)) return 1;
    if (run_signal_exit(SIGKILL)) return 1;

    /* waitpid with WNOHANG-ish call (options ignored) on -1 == wait. */
    int pid = fork();
    if (pid == 0) exit(3);
    int s = 0;
    int got = waitpid(-1, &s, 0);
    if (got != pid || !WIFEXITED(s) || WEXITSTATUS(s) != 3) {
        printf("wait_test: waitpid(-1) decode wrong got=%d pid=%d s=%d\n", got, pid, s);
        return 1;
    }

    printf("wait_test: PASS\n");
    return 0;
}
