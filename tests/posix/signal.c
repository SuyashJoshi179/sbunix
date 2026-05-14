/*
 * POSIX conformance test: <signal.h>
 *
 * Reference: docs/susv5-html/basedefs/signal.h.html
 *
 * Audited POSIX functions: kill, killpg, raise, sigaction, signal,
 *                          sigprocmask, sigpending, sigsuspend, sigaltstack,
 *                          sigemptyset, sigfillset, sigaddset, sigdelset,
 *                          sigismember, sighold, sigrelse, sigignore, sigset
 *
 * Excluded (in our libc but POSIX assigns elsewhere):
 *   - pause — POSIX: <unistd.h>
 *
 * Excluded (POSIX functions not implemented):
 *   - psiginfo, psignal, pthread_kill, pthread_sigmask, sigqueue,
 *     sigtimedwait, sigwait, sigwaitinfo
 *
 * Excluded (non-POSIX in our libc):
 *   - SA_NOMASK, SA_ONESHOT, SA_RESTORER, sa_restorer (Linux/BSD)
 */
#include <signal.h>

#define PIN __attribute__((unused)) static

/* Signal-handler pointer type (POSIX canonical form, no typedef). */
typedef void (*_posix_sighandler_t)(int);

/* DIVERGENCE: pid_t not visible from <signal.h> (our libc fails to
 * #include <sys/types.h>). POSIX requires <signal.h> to expose pid_t.
 * Uncomment after Phase 2 adds the include.
 * PIN int (*_pin_kill)(pid_t, int) = kill;
 * PIN int (*_pin_killpg)(pid_t, int) = killpg;
 */
PIN int (*_pin_raise)(int) = raise;
PIN int (*_pin_sigaction)(int, const struct sigaction *, struct sigaction *) = sigaction;
PIN _posix_sighandler_t (*_pin_signal)(int, _posix_sighandler_t) = signal;
PIN int (*_pin_sigprocmask)(int, const sigset_t *, sigset_t *) = sigprocmask;
PIN int (*_pin_sigpending)(sigset_t *) = sigpending;
PIN int (*_pin_sigsuspend)(const sigset_t *) = sigsuspend;
PIN int (*_pin_sigaltstack)(const stack_t *, stack_t *) = sigaltstack;
PIN int (*_pin_sighold)(int) = sighold;
PIN int (*_pin_sigrelse)(int) = sigrelse;
PIN int (*_pin_sigignore)(int) = sigignore;
PIN _posix_sighandler_t (*_pin_sigset)(int, _posix_sighandler_t) = sigset;

/* sigset_t manipulation — these are static inline in our header; taking
 * their address forces a definition + signature check. */
PIN int (*_pin_sigemptyset)(sigset_t *) = sigemptyset;
PIN int (*_pin_sigfillset)(sigset_t *) = sigfillset;
PIN int (*_pin_sigaddset)(sigset_t *, int) = sigaddset;
PIN int (*_pin_sigdelset)(sigset_t *, int) = sigdelset;
PIN int (*_pin_sigismember)(const sigset_t *, int) = sigismember;

__attribute__((unused))
static void _struct_fields(void) {
    struct sigaction sa;
    __builtin_memset(&sa, 0, sizeof sa);
    (void)sa.sa_handler; (void)sa.sa_mask; (void)sa.sa_flags;

    stack_t ss;
    __builtin_memset(&ss, 0, sizeof ss);
    (void)ss.ss_sp; (void)ss.ss_size; (void)ss.ss_flags;

    siginfo_t si;
    __builtin_memset(&si, 0, sizeof si);
    (void)si.si_signo; (void)si.si_code; (void)si.si_errno;
    (void)si.si_pid;   (void)si.si_uid;  (void)si.si_addr;
    (void)si.si_status;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    x |= SIG_BLOCK; x |= SIG_UNBLOCK; x |= SIG_SETMASK;
    x |= SIGHUP; x |= SIGINT; x |= SIGQUIT; x |= SIGILL;
    x |= SIGABRT; x |= SIGFPE; x |= SIGKILL; x |= SIGSEGV;
    x |= SIGPIPE; x |= SIGALRM; x |= SIGTERM; x |= SIGCHLD;
    x |= SIGCONT; x |= SIGSTOP; x |= SIGTSTP; x |= SIGTTIN;
    x |= SIGTTOU; x |= SIGUSR1; x |= SIGUSR2; x |= SIGBUS;
    x |= SIGSYS; x |= SIGTRAP; x |= SIGURG; x |= SIGXCPU;
    x |= SIGXFSZ; x |= SIGVTALRM; x |= SIGPROF;
    x |= SA_NOCLDSTOP; x |= SA_NOCLDWAIT; x |= SA_SIGINFO;
    x |= SA_ONSTACK; x |= SA_RESTART; x |= SA_NODEFER; x |= SA_RESETHAND;
    x |= SS_ONSTACK; x |= SS_DISABLE;
    (void)x;
    (void)SIG_DFL; (void)SIG_IGN; (void)SIG_ERR; (void)SIG_HOLD;
}
