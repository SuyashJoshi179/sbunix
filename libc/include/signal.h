#pragma once
#include <stdint.h>
#include <stddef.h>

#define NSIG    32

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG  23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO   29
#define SIGPOLL SIGIO
#define SIGPWR  30
#define SIGSYS  31

/* Real-time signals are not supported by SBUnix. NSIG == 32 above and a
 * 64-bit sigset_t mean SIGRTMIN/SIGRTMAX values would shift past the end
 * of the mask and break the sigaddset/sigdelset/sigismember helpers
 * (which do `(sigset_t)1 << sig`). Intentionally omitted; ports needing
 * RT signals will need NSIG widened and the helpers reworked. */

/* sigaction flags. SBUnix honors none of these meaningfully — handlers
 * always run on the same stack, signals always restart, etc. — but the
 * defines must exist so portable code compiles. */
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#define SA_INTERRUPT  0x20000000
#define SA_NOMASK     SA_NODEFER
#define SA_ONESHOT    SA_RESETHAND
#define SA_RESTORER   0x04000000

typedef struct {
    int          si_signo;
    int          si_errno;
    int          si_code;
    int          si_pid;
    int          si_uid;
    int          si_status;
    void        *si_addr;
    long         si_band;
    int          si_fd;
} siginfo_t;

#define SI_USER     0
#define SI_KERNEL   0x80
#define SI_QUEUE   -1
#define SI_TIMER   -2
#define SI_MESGQ   -3
#define SI_ASYNCIO -4

typedef struct {
    void  *ss_sp;
    int    ss_flags;
    size_t ss_size;
} stack_t;

typedef uint64_t sigset_t;

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

struct sigaction {
    /* Only write(2) and _exit(2) are async-signal-safe in this libc. */
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int kill(int pid, int sig);
int killpg(int pgid, int sig);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
sighandler_t signal(int sig, sighandler_t handler);
sighandler_t sigset(int sig, sighandler_t handler);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int raise(int sig);
int pause(void);
int sigsuspend(const sigset_t *mask);
int sighold(int sig);
int sigrelse(int sig);
int sigignore(int sig);

static inline int sigemptyset(sigset_t *s)            { *s = 0; return 0; }
static inline int sigfillset(sigset_t *s)             { *s = ~(sigset_t)0; return 0; }
static inline int sigaddset(sigset_t *s, int sig)     { *s |=  ((sigset_t)1 << sig); return 0; }
static inline int sigdelset(sigset_t *s, int sig)     { *s &= ~((sigset_t)1 << sig); return 0; }
static inline int sigismember(const sigset_t *s, int sig) { return (int)((*s >> sig) & 1); }
