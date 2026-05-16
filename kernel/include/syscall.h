#pragma once
#include <stdint.h>

// Syscall numbers. This header is the single source of truth — libc
// includes it transitively (via the libc syscall wrappers), and any
// libc .c that issues a raw ecall (libc/signal.c, libc/ids.c,
// libc/proc_grp.c, etc.) hardcodes the matching constant. When adding
// a new syscall, update this file plus the libc wrapper that uses it.
#define SYS_exit    1
#define SYS_write   2
#define SYS_exec    3
#define SYS_open    4
#define SYS_read    5
#define SYS_close   6
#define SYS_wait    7
#define SYS_getpid  8
#define SYS_fork    9
#define SYS_spawn   10
#define SYS_getppid    11
#define SYS_yield      12
#define SYS_sleep      13  // arg0: milliseconds

// Phase 4: file descriptor syscalls
// SYS_open (4), SYS_read (5), SYS_close (6) re-use existing slot numbers.
#define SYS_dup        14
#define SYS_dup2       15
#define SYS_lseek      16
#define SYS_fstat      17
#define SYS_getdents64 18
#define SYS_chdir      19
#define SYS_getcwd     20

// Phase 5: writable sbfs syscalls
#define SYS_mkdir      21
#define SYS_unlink     22

// Phase 10 prep: hard link + rename
#define SYS_link       25  // (oldpath, newpath)
#define SYS_rename     26  // (oldpath, newpath)

// Phase 6: pipes and exec with argv
#define SYS_pipe       23
#define SYS_execv      24

// Phase 7: memory management
#define SYS_sbrk       70
#define SYS_mmap       71
#define SYS_munmap     72

// Phase 8a: time
#define SYS_clock_gettime  80   // (clockid, struct timespec *)
#define SYS_gettimeofday   81   // (struct timeval *, NULL)
#define SYS_nanosleep      82   // (const struct timespec *req, struct timespec *rem)

// Filesystem mount
#define SYS_mount          83   // (const char *target, const char *fstype)

// Phase 8a (cont.): alarm
#define SYS_alarm          84   // (unsigned secs) -> prior remaining secs

// Filesystem: truncate(2) / ftruncate(2)
#define SYS_truncate       85   // (const char *path, off_t length)
#define SYS_ftruncate      86   // (int fd, off_t length)

// Filesystem: symlink(2)
#define SYS_symlink        87   // (const char *target, const char *linkpath)

// Phase 8a (cont.): wall-clock adjustment and resolution
#define SYS_clock_settime  88   // (clockid, const struct timespec *)
#define SYS_clock_getres   89   // (clockid, struct timespec *)

// Phase 8b: signals
#define SYS_kill           90   // (pid, sig)
#define SYS_sigaction      91   // (sig, const struct sigaction *act, struct sigaction *oldact)
#define SYS_sigprocmask    92   // (how, const sigset_t *set, sigset_t *oldset)
#define SYS_sigreturn      93   // ()
#define SYS_pause          94   // ()
#define SYS_sigsuspend     27   // (const sigset_t *mask)
#define SYS_sigpending     28   // (sigset_t *set)
#define SYS_killpg         29   // (pgid, sig)
#define SYS_sigaltstack    30   // (const stack_t *ss, stack_t *oss)

// Phase 8a: uid/gid stubs
#define SYS_getuid        100
#define SYS_geteuid       101
#define SYS_getgid        102
#define SYS_getegid       103
#define SYS_setuid        104   // (uid)
#define SYS_setgid        105   // (gid)

// Phase 8c: ioctl
#define SYS_ioctl         110   // (fd, cmd, arg)

// Phase 9e: memory info
#define SYS_meminfo       111   // () -> free physical pages
#define SYS_readlink      112
#define SYS_lstat         113
#define SYS_access        114   // (const char *path, int mode) — F_OK/R_OK/W_OK/X_OK
#define SYS_msync         115
#define SYS_times         116   // (struct tms *buf)
#define SYS_getrlimit     117   // (resource, struct rlimit *)
#define SYS_setrlimit     118   // (resource, const struct rlimit *)

// Filesystem: per-descriptor flags
#define SYS_fcntl          119  // (fd, cmd, arg) — F_GETFD / F_SETFD only

// POSIX positional I/O — read/write at an explicit offset without
// touching the file cursor. Pipes return ESPIPE.
#define SYS_pread          120  // (fd, buf, count, offset)
#define SYS_pwrite         121  // (fd, buf, count, offset)
#define SYS_execve         122  // (path, argv, envp) — envp propagates
#define SYS_utimensat      123  // (dirfd, path, struct timespec[2], flags)

// fcntl commands and flags the kernel honors. Values must match
// libc/include/fcntl.h verbatim — they are part of the kernel ABI.
#define F_GETFD            1
#define F_SETFD            2
#define F_GETFL            3
#define F_SETFL            4
#define FD_CLOEXEC         1
#define O_CLOEXEC          02000000

#define SYS_wait4         106   // (pid, *status, options, *rusage)

// Job control: process groups / sessions
#define SYS_setpgid        95   // (pid, pgid)
#define SYS_getpgid        96   // (pid)
#define SYS_getpgrp        97   // ()
#define SYS_setsid         98   // ()
#define SYS_getsid         99   // (pid)
// Trap-frame word indices for registers saved by trap.S.
// Frame layout (8 bytes per slot, from sp):
//   offset   0: x1  (ra)
//   offset   8: x2  (sp) — original user/kernel sp
//   offset  16: x3  (gp)
//   ...
//   offset  72: x10 (a0)   → TF_A0
//   offset  80: x11 (a1)   → TF_A1
//   offset  88: x12 (a2)   → TF_A2
//   offset  96: x13 (a3)   → TF_A3
//   ...
//   offset 128: x17 (a7)   → TF_A7
//   ...
//   offset 248: sepc       → TF_SEPC
//   offset 256: sstatus    → TF_SSTATUS
#define TF_RA       0   /* x1 = ra */
#define TF_A0       9
#define TF_A1      10
#define TF_A2      11
#define TF_A3      12
#define TF_A4      13
#define TF_A5      14
#define TF_A7      16
#define TF_SEPC    31
#define TF_SSTATUS 32

// Dispatch a syscall.  trapframe points to the trap frame on the kernel stack.
// Returns the value to place in a0 on return to user.
int64_t syscall_dispatch(uint64_t sysnum, uint64_t *trapframe);
