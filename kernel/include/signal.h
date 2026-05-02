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
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);  /* libc fills with __sigtramp address */
};

/* sigprocmask how values */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* Signal frame written by kernel onto user stack during delivery */
#define SIGFRAME_MAGIC 0x5342534947464DULL  /* "SBSIGFRM" */

struct sigframe {
    uint64_t magic;
    uint64_t saved_mask;
    uint64_t saved_trapframe[36];   /* 288 bytes = 36 × uint64_t */
    uint64_t _pad[2];               /* pad to 320 bytes, multiple of 16 */
};

/* Default action codes */
#define ACT_TERM 0
#define ACT_IGN  1
#define ACT_CORE 2  /* treated as TERM — no core dumps */
#define ACT_STOP 3
#define ACT_CONT 4

/* Kernel-internal API (implementations in kernel/signal.c) */
struct pcb;
void send_signal(struct pcb *target, int sig);
void send_signal_by_pid(int pid, int sig);
int  send_signal_pgrp(int pgid, int sig);
void check_signals(uint64_t *trapframe);

/* Signal syscall implementations (kernel/signal.c). */
int64_t sys_kill(int pid, int sig);
int64_t sys_sigaction(int sig, const struct sigaction *act,
                      struct sigaction *oldact);
int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int64_t sys_sigreturn(uint64_t *trapframe);
int64_t sys_pause(void);

/* Raw bitmask helper — "is any pending signal currently unblocked?".
 * Used by send_signal to decide whether to wake a sleeper so that
 * check_signals can consume the bit (even for SIG_IGN / default-ignored). */
static inline int sig_has_pending(uint64_t pending, uint64_t blocked) {
    return (pending & ~blocked) != 0;
}

/* Returns 1 if any pending+unblocked signal would actually be visible
 * (run a handler or terminate the process).  Ignored signals — including
 * the default-ignore set (SIGCHLD, SIGCONT) — don't count.  Blocking
 * syscalls use this to decide whether to return -EINTR. */
int sig_has_actionable(struct pcb *p);
