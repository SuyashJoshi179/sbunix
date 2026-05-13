#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "syscall_priv.h"

extern void __sigtramp(void);

static long ecall0(long num) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}

static long ecall2(long num, long a0, long a1) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

static long ecall1(long num, long a0) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}

static long ecall3(long num, long a0, long a1, long a2) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1), "r"(_a2) : "memory");
    return _a0;
}

int kill(int pid, int sig) {
    return (int)syscall_ret(ecall2(90, (long)pid, (long)sig));
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    struct sigaction kact;
    const struct sigaction *actp = act;

    if (act) {
        kact = *act;
        kact.sa_restorer = __sigtramp;
        actp = &kact;
    }

    return (int)syscall_ret(ecall3(91, (long)sig, (long)actp, (long)oldact));
}

sighandler_t signal(int sig, sighandler_t handler) {
    struct sigaction sa;
    struct sigaction old;

    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = __sigtramp;

    if (sigaction(sig, &sa, &old) < 0)
        return SIG_ERR;
    return old.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return (int)syscall_ret(ecall3(92, (long)how, (long)set, (long)oldset));
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int pause(void) {
    return (int)syscall_ret(ecall0(94));
}

int sigpending(sigset_t *set) {
    return (int)syscall_ret(ecall1(28, (long)set));
}

/* killpg(pgid, sig) — send sig to every member of process group pgid.
 * Usually implemented as kill(-pgid, sig), but our kernel reads
 * pid == -1 as POSIX broadcast (see kernel/signal.c sys_kill), so
 * pgid == 1 would broadcast instead of targeting pgrp 1. POSIX also
 * has pgid == 0 mean "calling process's pgrp", which kill() can't
 * express. Reject both rather than misbehave silently; a proper fix
 * would be a dedicated SYS_killpg with explicit pgid semantics. */
int killpg(int pgid, int sig) {
    if (pgid <= 0 || pgid == 1) { errno = EINVAL; return -1; }
    return kill(-pgid, sig);
}

/* sigaddset/sigdelset/sigismember in <signal.h> are inline and do
 * `1ULL << sig` with no bounds check, so anything outside [1, NSIG)
 * is UB. Helpers below that build a one-element set must validate
 * sig before touching sigaddset. */
static int valid_signo(int sig) { return sig > 0 && sig < NSIG; }

int sighold(int sig) {
    if (!valid_signo(sig)) { errno = EINVAL; return -1; }
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, sig);
    return sigprocmask(SIG_BLOCK, &s, NULL);
}

int sigrelse(int sig) {
    if (!valid_signo(sig)) { errno = EINVAL; return -1; }
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, sig);
    return sigprocmask(SIG_UNBLOCK, &s, NULL);
}

int sigignore(int sig) {
    struct sigaction sa = { 0 };
    sa.sa_handler = SIG_IGN;
    return sigaction(sig, &sa, NULL);
}

/* sigset(sig, handler) — System V signal-with-mask. Behaves like
 * signal(sig, handler) but with sig added to sa_mask while the handler
 * runs (so the signal can't recursively fire on itself). This
 * implementation does NOT support the historical SIG_HOLD sentinel
 * (often (sighandler_t)2); callers wanting to block a signal must
 * use sighold()/sigrelse() explicitly. We reject the SIG_HOLD-shaped
 * value so a caller that relies on it gets a clear error rather than
 * silently installing a "handler" at address 2. Returns previous
 * disposition. */
sighandler_t sigset(int sig, sighandler_t handler) {
    if (!valid_signo(sig)) { errno = EINVAL; return SIG_ERR; }
    if (handler == (sighandler_t)2) { errno = EINVAL; return SIG_ERR; }
    struct sigaction sa = { 0 };
    struct sigaction old;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, sig);
    if (sigaction(sig, &sa, &old) < 0) return SIG_ERR;
    return old.sa_handler;
}
