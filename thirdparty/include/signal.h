#pragma once
#include_next <signal.h>

/* Standard Linux signal numbers for signals SBUnix tests do not exercise.
 * Busybox (BB_FATAL_SIGS in libbb.h, etc.) references some of these
 * unconditionally at compile time and fails to build if the libc header
 * omits them. Wrapping these as fallbacks lets students skip implementing
 * signals the assignment never tests. Signals that ARE tested
 * (SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE,
 * SIGKILL, SIGSEGV, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2, SIGPIPE, SIGCHLD,
 * SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU) are intentionally NOT shimmed
 * here -- the student libc must provide them. */
#ifndef SIGSTKFLT
#define SIGSTKFLT  16
#endif
#ifndef SIGURG
#define SIGURG     23
#endif
#ifndef SIGXCPU
#define SIGXCPU    24
#endif
#ifndef SIGXFSZ
#define SIGXFSZ    25
#endif
#ifndef SIGVTALRM
#define SIGVTALRM  26
#endif
#ifndef SIGPROF
#define SIGPROF    27
#endif
#ifndef SIGWINCH
#define SIGWINCH   28
#endif
#ifndef SIGIO
#define SIGIO      29
#endif
#ifndef SIGPOLL
#define SIGPOLL    SIGIO
#endif
#ifndef SIGPWR
#define SIGPWR     30
#endif
#ifndef SIGSYS
#define SIGSYS     31
#endif

/* busybox (e.g. ash.c sigmode[NSIG-1]) needs NSIG to cover every SIG* it can
 * reference. Force NSIG to at least 32 so the shimmed signals above fit. */
#if !defined(NSIG) || NSIG < 32
#undef NSIG
#define NSIG 32
#endif

/* Sigaction flag: busybox uses this in libbb/signals.c. Value matches Linux. */
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#endif

sighandler_t signal(int sig, sighandler_t handler);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int raise(int sig);
int sigsuspend(const sigset_t *mask);
