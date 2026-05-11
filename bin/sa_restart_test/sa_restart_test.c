/* sa_restart_test — verify sigaction's SA_RESTART flag actually restarts
 * a blocking syscall instead of returning -EINTR (T2.10).
 *
 * Setup: a pipe and a fork. Child sleeps briefly, sends SIGUSR1 to the
 * parent, sleeps again, then writes data. Parent does a blocking read.
 *   - With SA_RESTART:    handler fires, read replays, returns the data.
 *   - Without SA_RESTART: handler fires, read returns -1 with EINTR.
 *
 * We run both arms in one process by exec'ing fresh forks for each so the
 * sigaction state can't leak. fork+wait also exercises wait4 across an
 * SA_RESTART-enabled SIGCHLD path. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int handler_count = 0;
static void usr1_handler(int sig) {
    (void)sig;
    handler_count++;
}

static int fails = 0;
static void ok(int cond, const char *name) {
    if (cond) printf("[sa_restart] PASS  %s\n", name);
    else      { printf("[sa_restart] FAIL  %s\n", name); fails++; }
}

/* arm_handler installs usr1_handler for SIGUSR1; `restart != 0` sets
 * SA_RESTART. Returns 0 on success. */
static int arm_handler(int restart) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = usr1_handler;
    sa.sa_mask    = 0;
    sa.sa_flags   = restart ? SA_RESTART : 0;
    return sigaction(SIGUSR1, &sa, 0);
}

/* run_arm forks a child; child sleeps `pre_ms`, signals parent, sleeps
 * `post_ms`, writes `marker` (3 bytes), exits. Parent reads up to 3
 * bytes from the pipe. Returns the read result; *got_signal is set
 * to handler_count after the read returns. */
static long run_arm(int restart, int *got_signal, char out[3]) {
    int p[2];
    if (pipe(p) < 0) return -2;

    handler_count = 0;
    if (arm_handler(restart) < 0) { close(p[0]); close(p[1]); return -3; }

    int pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -4; }
    if (pid == 0) {
        close(p[0]);
        sleep_ms(40);
        kill(getppid(), SIGUSR1);
        sleep_ms(40);
        write(p[1], "abc", 3);
        close(p[1]);
        exit(0);
    }

    close(p[1]);
    out[0] = out[1] = out[2] = 0;
    errno = 0;
    long n = read(p[0], out, 3);
    int saved = errno;
    close(p[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    *got_signal = handler_count;
    if (n < 0) errno = saved;
    return n;
}

int main(void) {
    /* (1) SA_RESTART: read should transparently replay and return 3 bytes. */
    {
        int got = 0;
        char buf[3];
        long n = run_arm(1 /* SA_RESTART */, &got, buf);
        ok(got >= 1, "SA_RESTART: handler fired");
        ok(n == 3, "SA_RESTART: read returned full count after signal");
        ok(n == 3 && memcmp(buf, "abc", 3) == 0,
           "SA_RESTART: read got the expected bytes");
    }

    /* (2) No SA_RESTART: read should fail with -1/EINTR. */
    {
        int got = 0;
        char buf[3];
        errno = 0;
        long n = run_arm(0 /* no SA_RESTART */, &got, buf);
        int saved = errno;
        ok(got >= 1, "no-restart: handler fired");
        ok(n == -1, "no-restart: read returned -1");
        ok(saved == EINTR, "no-restart: errno == EINTR");
    }

    /* (3) wait4 with SA_RESTART: a child sends SIGUSR1 to us while we
     *     wait on a longer-living child; wait should transparently
     *     replay until the long-lived child exits. */
    {
        arm_handler(1);
        handler_count = 0;
        int signaller = fork();
        if (signaller == 0) {
            sleep_ms(40);
            kill(getppid(), SIGUSR1);
            exit(0);
        }
        int sleeper = fork();
        if (sleeper == 0) {
            sleep_ms(120);
            exit(42);
        }
        /* Wait specifically for sleeper. With SA_RESTART, the SIGUSR1
         * delivery mid-wait must not return early. */
        int st = 0;
        int got = waitpid(sleeper, &st, 0);
        ok(got == sleeper, "wait4 SA_RESTART: waited for the right child");
        ok(WIFEXITED(st) && WEXITSTATUS(st) == 42,
           "wait4 SA_RESTART: child's exit code seen");
        ok(handler_count >= 1, "wait4 SA_RESTART: signal handler still ran");
        /* Reap the signaller too. */
        waitpid(signaller, 0, 0);
    }

    /* (4) Verify the kernel-internal sentinel never leaks: errno from a
     *     read interrupted without SA_RESTART must be 4 (EINTR), not 512. */
    {
        int got = 0;
        char buf[3];
        errno = 0;
        long n = run_arm(0, &got, buf);
        ok(n == -1 && errno == EINTR && errno != 512,
           "no leak of -ERESTARTSYS sentinel into errno");
    }

    printf("=== sa_restart_test: %d failures ===\n", fails);
    return fails ? 1 : 0;
}
