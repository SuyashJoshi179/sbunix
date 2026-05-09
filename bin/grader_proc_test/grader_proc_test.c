/*
 * grader_proc_test — Process lifecycle, signals, shell exit codes.
 *
 * Tests fork/exec/wait/exit, shell exit code propagation (sh -c "exit N"),
 * signal masking, kill(-pgid), and pipes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[grader_proc_test] PASS  %s\n", name);
    } else {
        printf("[grader_proc_test] FAIL  %s\n", name);
        fails++;
    }
}

static volatile int handler_fired = 0;
static void usr1_handler(int sig) {
    (void)sig;
    handler_fired = 1;
}

int main(void) {
    printf("=== grader_proc_test ===\n");

    /* 1. fork returns correct values */
    {
        int parent_pid = getpid();
        int pid = fork();
        if (pid == 0) {
            int ppid = getppid();
            exit(ppid == parent_pid ? 0 : 1);
        }
        check(pid > 0, "fork() returns positive pid in parent");
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child getppid() matches parent getpid()");
    }

    /* 2. getpid consistency */
    {
        int pid = fork();
        if (pid == 0) {
            int my_pid = getpid();
            exit(my_pid > 0 ? 0 : 1);
        }
        int st;
        int got = waitpid(pid, &st, 0);
        check(got == pid, "waitpid returns correct child pid");
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "child getpid() returns positive value");
    }

    /* 3. Exit status propagation */
    {
        int pid = fork();
        if (pid == 0) exit(42);
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st), "WIFEXITED after exit(42)");
        check(WEXITSTATUS(st) == 42, "WEXITSTATUS == 42");
    }

    /* 4. Shell exit code propagation: sh -c "exit N"
     * (The #2 most common grader failure across teams) */
    {
        int codes[] = {0, 1, 7, 42};
        int ncodes = 4;
        for (int i = 0; i < ncodes; i++) {
            int pid = fork();
            if (pid == 0) {
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "exit %d", codes[i]);
                char *args[] = {"/bin/sh", "-c", cmd, 0};
                execv("/bin/sh", args);
                exit(127); /* exec failed */
            }
            int st;
            waitpid(pid, &st, 0);
            char desc[64];
            snprintf(desc, sizeof(desc),
                     "sh -c 'exit %d' => WEXITSTATUS==%d", codes[i], codes[i]);
            if (WIFEXITED(st)) {
                check(WEXITSTATUS(st) == codes[i], desc);
            } else {
                check(0, desc);
            }
        }
    }

    /* 5. execv replaces process image */
    {
        int pid = fork();
        if (pid == 0) {
            char *args[] = {"/bin/echo", "exec_works", 0};
            execv("/bin/echo", args);
            exit(99);
        }
        int st;
        waitpid(pid, &st, 0);
        check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
              "execv(/bin/echo) child exits 0");
    }

    /* 6. kill(child, SIGKILL) */
    {
        int pid = fork();
        if (pid == 0) {
            while (1) sched_yield();
            exit(0);
        }
        kill(pid, SIGKILL);
        int st;
        waitpid(pid, &st, 0);
        check(WIFSIGNALED(st), "WIFSIGNALED after SIGKILL");
        check(WTERMSIG(st) == SIGKILL, "WTERMSIG == SIGKILL");
    }

    /* 7. Signal handler — SIGUSR1 */
    {
        handler_fired = 0;
        signal(SIGUSR1, usr1_handler);
        raise(SIGUSR1);
        check(handler_fired == 1, "SIGUSR1 handler fires on raise()");
        signal(SIGUSR1, SIG_DFL);
    }

    /* 8. Signal masking — blocked signal stays pending until unblocked */
    {
        handler_fired = 0;
        signal(SIGUSR1, usr1_handler);

        sigset_t block, old;
        sigemptyset(&block);
        sigaddset(&block, SIGUSR1);
        sigprocmask(SIG_BLOCK, &block, &old);

        raise(SIGUSR1);
        check(handler_fired == 0,
              "blocked SIGUSR1: handler NOT fired while blocked");

        sigprocmask(SIG_UNBLOCK, &block, 0);
        check(handler_fired == 1,
              "blocked SIGUSR1: handler fires after unblock");

        signal(SIGUSR1, SIG_DFL);
        sigprocmask(SIG_SETMASK, &old, 0);
    }

    /* 9. Pipe: parent writes, child reads */
    {
        int pfd[2];
        int rc = pipe(pfd);
        check(rc == 0, "pipe() succeeds");
        if (rc == 0) {
            int pid = fork();
            if (pid == 0) {
                close(pfd[1]);
                char buf[16];
                long n = read(pfd[0], buf, sizeof(buf));
                close(pfd[0]);
                exit(n == 5 && memcmp(buf, "hello", 5) == 0 ? 0 : 1);
            }
            close(pfd[0]);
            write(pfd[1], "hello", 5);
            close(pfd[1]);
            int st;
            waitpid(pid, &st, 0);
            check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "pipe: child reads data written by parent");
        }
    }

    /* 10. Pipe EOF — writer closes, reader gets 0 */
    {
        int pfd[2];
        if (pipe(pfd) == 0) {
            int pid = fork();
            if (pid == 0) {
                close(pfd[1]);
                char buf[16];
                long n = read(pfd[0], buf, sizeof(buf));
                close(pfd[0]);
                exit(n == 0 ? 0 : 1);
            }
            close(pfd[0]);
            close(pfd[1]); /* writer closes immediately */
            int st;
            waitpid(pid, &st, 0);
            check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "pipe EOF: reader gets 0 after writer closes");
        }
    }

    /* 11. kill(-pgid) reaches all group members */
    {
        int children[3];
        int all_forked = 1;
        /* Use parent's pgid so all children are in the same group */
        for (int i = 0; i < 3; i++) {
            children[i] = fork();
            if (children[i] == 0) {
                while (1) sched_yield();
                exit(0);
            }
            if (children[i] < 0) all_forked = 0;
            else setpgid(children[i], children[0]);
        }
        if (all_forked) {
            /* Kill the entire process group */
            kill(-children[0], SIGKILL);
            int all_killed = 1;
            for (int i = 0; i < 3; i++) {
                int st;
                waitpid(children[i], &st, 0);
                if (!WIFSIGNALED(st)) all_killed = 0;
            }
            check(all_killed, "kill(-pgid, SIGKILL) reaches all 3 children");
        }
    }

    printf("=== grader_proc_test: %d failures ===\n", fails);
    return fails;
}
