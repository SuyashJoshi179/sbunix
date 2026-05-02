#pragma once
#include <stdint.h>

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

typedef uint64_t sigset_t;

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)

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
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
sighandler_t signal(int sig, sighandler_t handler);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int raise(int sig);
int pause(void);

static inline int sigemptyset(sigset_t *s)            { *s = 0; return 0; }
static inline int sigfillset(sigset_t *s)             { *s = ~(sigset_t)0; return 0; }
static inline int sigaddset(sigset_t *s, int sig)     { *s |=  ((sigset_t)1 << sig); return 0; }
static inline int sigdelset(sigset_t *s, int sig)     { *s &= ~((sigset_t)1 << sig); return 0; }
static inline int sigismember(const sigset_t *s, int sig) { return (int)((*s >> sig) & 1); }
