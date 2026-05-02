// Verify sa_mask is honored: signals listed in sa_mask must be blocked
// for the duration of the handler, then automatically unblocked when the
// handler returns (via sigreturn restoring the saved mask).
#include <signal.h>
#include <stdio.h>

static volatile int usr2_seen = 0;
static volatile int usr2_seen_inside_usr1 = -1;
static volatile int usr1_returned = 0;

static int pass, fail;
static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else      { printf("FAIL  %s\n", name); fail++; }
}

static void on_usr2(int sig) {
    (void)sig;
    usr2_seen++;
}

static void on_usr1(int sig) {
    (void)sig;
    // SIGUSR2 is in our sa_mask, so raising it now should leave it
    // pending (not delivered) until this handler returns.
    raise(SIGUSR2);
    usr2_seen_inside_usr1 = usr2_seen;
    usr1_returned = 1;
}

// Verify the handler's own signal is also blocked (POSIX default,
// no SA_NODEFER): raising SIGUSR2 a second time inside on_usr2 must
// not recurse.
static volatile int usr2_recursion_depth = 0;
static volatile int usr2_max_depth = 0;
static void on_usr2_nodefer_test(int sig) {
    (void)sig;
    usr2_recursion_depth++;
    if (usr2_recursion_depth > usr2_max_depth)
        usr2_max_depth = usr2_recursion_depth;
    static int reentered = 0;
    if (!reentered) {
        reentered = 1;
        raise(SIGUSR2);  // own signal — should be blocked, stay pending
    }
    usr2_recursion_depth--;
}

int main(void) {
    struct sigaction sa;

    // Install on_usr2 first.
    sa.sa_handler = on_usr2;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    sigaction(SIGUSR2, &sa, 0);

    // Install on_usr1 with sa_mask blocking SIGUSR2 during handler.
    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR2);
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    sigaction(SIGUSR1, &sa, 0);

    raise(SIGUSR1);

    check(usr1_returned == 1,           "SIGUSR1 handler ran");
    check(usr2_seen_inside_usr1 == 0,   "SIGUSR2 blocked inside SIGUSR1 (sa_mask)");
    check(usr2_seen == 1,               "SIGUSR2 delivered after sigreturn restored mask");

    // Now test self-deferral of own signal.
    sa.sa_handler = on_usr2_nodefer_test;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = 0;
    sigaction(SIGUSR2, &sa, 0);

    usr2_recursion_depth = 0;
    usr2_max_depth = 0;
    raise(SIGUSR2);
    check(usr2_max_depth == 1, "handler's own signal blocked during handler (no recursion)");

    printf("sigmask_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
