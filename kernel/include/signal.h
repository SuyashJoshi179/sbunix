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

/* sigaction sa_flags (matches libc/include/signal.h). SA_RESTART and
 * SA_ONSTACK are honored by the kernel; other flags are stored but
 * ignored. */
#define SA_RESTART  0x10000000
#define SA_ONSTACK  0x08000000

/* POSIX alternate signal stack. ss_flags bits and size floor must match
 * libc/include/signal.h. MINSIGSTKSZ is the smallest stack the kernel
 * accepts via sigaltstack(); anything smaller fails -ENOMEM. */
typedef struct {
    void    *ss_sp;
    int      ss_flags;
    uint64_t ss_size;
} stack_t;

#define SS_ONSTACK   1   /* returned in oss.ss_flags while handler runs on alt */
#define SS_DISABLE   2   /* set in ss.ss_flags to clear; default state */
#define MINSIGSTKSZ  2048

/* Signal frame written by kernel onto user stack during delivery */
#define SIGFRAME_MAGIC 0x5342534947464DULL  /* "SBSIGFRM" */

struct sigframe {
    uint64_t magic;
    uint64_t saved_mask;
    uint64_t saved_trapframe[36];   /* 288 bytes = 36 × uint64_t */
    uint64_t saved_on_altstack;     /* p->sig_on_altstack at delivery */
    uint64_t _pad;                  /* pad to 320 bytes, multiple of 16 */
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

/* Called from trap_handler at the end of a U-mode ecall. `ret` is the
 * kernel's syscall return value (already written to trapframe[TF_A0]);
 * `orig_a0` is the first syscall argument captured before dispatch.
 *
 * If `ret == -ERESTARTSYS` (sentinel from a blocking op interrupted by a
 * signal) and the signal about to be delivered has a custom handler with
 * SA_RESTART set, the trapframe is rewound to re-execute the ecall after
 * sigreturn. Otherwise the sentinel is translated to -EINTR. */
void check_signals_after_syscall(uint64_t *trapframe, int64_t ret,
                                 uint64_t orig_a0);

/* Signal syscall implementations (kernel/signal.c). */
int64_t sys_kill(int pid, int sig);
int64_t sys_sigaction(int sig, const struct sigaction *act,
                      struct sigaction *oldact);
int64_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int64_t sys_sigreturn(uint64_t *trapframe);
int64_t sys_pause(void);
int64_t sys_sigsuspend(const sigset_t *mask);
int64_t sys_sigpending(sigset_t *set);
int64_t sys_killpg(int pgid, int sig);
int64_t sys_sigaltstack(const stack_t *ss, stack_t *oss);

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
