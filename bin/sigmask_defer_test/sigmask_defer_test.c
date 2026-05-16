#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Grader feedback (multiple submissions):
 *   "When a process blocks a signal and sends that signal to itself,
 *    the handler appears to fire before the blocking call's effect is
 *    respected."
 *
 * Block SIGUSR1, install a handler that sets a flag, send the signal
 * to self, check the flag (must still be 0), then unblock and check
 * again (must now be 1).
 */

static volatile int fired = 0;
static void handler(int sig) { (void)sig; fired = 1; }

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handler;
    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        printf("sigmask_defer_test: sigaction failed\n");
        return 1;
    }

    sigset_t mask = (sigset_t)1ULL << SIGUSR1;
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
        printf("sigmask_defer_test: sigprocmask block failed\n");
        return 1;
    }

    if (kill(getpid(), SIGUSR1) < 0) {
        printf("sigmask_defer_test: kill failed\n");
        return 1;
    }

    /* The handler must not have fired while the signal is blocked. */
    if (fired != 0) {
        printf("sigmask_defer_test: handler FIRED while blocked (fired=%d)\n",
               fired);
        return 1;
    }

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) {
        printf("sigmask_defer_test: sigprocmask unblock failed\n");
        return 1;
    }

    /* The unblock should immediately deliver the pending signal. */
    if (fired != 1) {
        printf("sigmask_defer_test: handler did NOT fire after unblock (fired=%d)\n",
               fired);
        return 1;
    }

    printf("sigmask_defer_test: PASS\n");
    return 0;
}
