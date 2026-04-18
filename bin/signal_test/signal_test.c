#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int term_seen;
static volatile int usr1_seen;

static void on_term(int sig) {
    (void)sig;
    term_seen++;
    raise(SIGUSR1);
}

static void on_usr1(int sig) {
    (void)sig;
    usr1_seen++;
}

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    struct sigaction sa;
    sigset_t set;

    sa.sa_handler = on_term;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    check(sigaction(SIGTERM, &sa, 0) == 0, "sigaction(SIGTERM) installs handler");

    sa.sa_handler = on_usr1;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    check(sigaction(SIGUSR1, &sa, 0) == 0, "sigaction(SIGUSR1) installs handler");

    sa.sa_handler = on_term;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    check(sigaction(SIGKILL, &sa, 0) == -EINVAL, "sigaction(SIGKILL) rejected");

    check(kill(getpid(), 0) == 0, "kill(self, 0) succeeds");
    check(kill(99999, 0) == -ESRCH, "kill(missing, 0) returns -ESRCH");

    set = (1ULL << SIGTERM);
    check(sigprocmask(SIG_BLOCK, &set, 0) == 0, "sigprocmask block SIGTERM");
    term_seen = 0;
    check(kill(getpid(), SIGTERM) == 0, "kill(self, SIGTERM) while blocked succeeds");
    check(term_seen == 0, "blocked SIGTERM not delivered immediately");

    check(sigprocmask(SIG_UNBLOCK, &set, 0) == 0, "sigprocmask unblock SIGTERM");
    check(kill(getpid(), SIGTERM) == 0, "kill(self, SIGTERM) after unblock succeeds");
    check(term_seen > 0, "SIGTERM handler ran");
    check(usr1_seen > 0, "nested SIGUSR1 handler ran");

    printf("signal_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
