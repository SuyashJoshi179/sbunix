/*
 * POSIX conformance test: <sys/wait.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_wait.h.html
 *
 * Audited POSIX functions: wait, waitpid
 * Required macros: WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG, WIFSTOPPED,
 *                  WSTOPSIG, WIFCONTINUED, WNOHANG, WUNTRACED, WCONTINUED
 *
 * Excluded (not implemented): waitid
 * Excluded (non-POSIX): wait4
 */
#include <sys/wait.h>

#define PIN __attribute__((unused)) static

PIN pid_t (*_pin_wait)(int *) = wait;
PIN pid_t (*_pin_waitpid)(pid_t, int *, int) = waitpid;

__attribute__((unused))
static void _macro_checks(int s) {
    int x = 0;
    x |= WIFEXITED(s);
    x |= WEXITSTATUS(s);
    x |= WIFSIGNALED(s);
    x |= WTERMSIG(s);
    x |= WIFSTOPPED(s);
    x |= WSTOPSIG(s);
    x |= WIFCONTINUED(s);
    x |= WNOHANG;
    x |= WUNTRACED;
    x |= WCONTINUED;
    (void)x;
}
