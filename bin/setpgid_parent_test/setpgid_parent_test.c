/*
 * setpgid_parent_test — verify the parent path of setpgid() works.
 *
 * POSIX: a process may set its own pgid OR set the pgid of a direct
 * child within the same session. The parent path is critical for
 * race-free shell job control:
 *
 *   1. parent: pid = fork()
 *   2. parent: setpgid(pid, pid)              ← MUST succeed
 *   3. parent: tcsetpgrp(0, pid)               (foreground group)
 *   4. parent: waitpid(-pid, &st, 0)           (wait by pgrp)
 *   5. (concurrently) child: setpgid(0, 0)     (idempotent)
 *
 * Without step 2 working, step 4 may match no children (if the child
 * hasn't reached step 5 yet) and return immediately. Symptom in the
 * shell: prompt prints before the foreground command's output.
 *
 * This test forks, has the parent setpgid the child, and verifies:
 *   - parent's setpgid(child_pid, child_pid) returns 0
 *   - getpgid(child_pid) reflects the new value
 *   - waitpid(-child_pid, ...) actually blocks until the child exits
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int pass = 0, fail = 0;
static void chk(int cond, const char *msg) {
    if (cond) { printf("[setpgid_parent_test] PASS  %s\n", msg); pass++; }
    else      { printf("[setpgid_parent_test] FAIL  %s\n", msg); fail++; }
}

int main(void) {
    int pid = fork();
    if (pid < 0) { printf("fork failed\n"); return 1; }
    if (pid == 0) {
        /* Child: spin a bit then exit, so parent has time to setpgid
         * and waitpid us before we self-exit. The point is to verify
         * parent's setpgid path works regardless of child timing. */
        sleep_ms(200);
        exit(42);
    }

    /* Parent: try to set the child's pgid before the child runs. */
    int rc = setpgid(pid, pid);
    chk(rc == 0, "parent setpgid(child, child) returns 0");

    int actual = getpgid(pid);
    chk(actual == pid, "getpgid(child) reports the value parent set");

    /* Now wait by negative pgid — must block until the child exits. */
    int st = -1;
    int got = waitpid(-pid, &st, 0);
    chk(got == pid, "waitpid(-child_pgid) returned the child's pid");
    chk(WIFEXITED(st) && WEXITSTATUS(st) == 42,
        "child exited normally with status 42 (waitpid actually blocked)");

    /* Negative test: setpgid on a non-child should fail. */
    rc = setpgid(1, 1);
    chk(rc < 0, "parent setpgid on non-child returns negative (EPERM)");

    if (fail == 0) printf("setpgid_parent_test: PASS (%d tests)\n", pass);
    else           printf("setpgid_parent_test: FAIL (%d/%d failed)\n",
                          fail, pass + fail);
    return fail ? 1 : 0;
}
