#pragma once
#include <stdint.h>

// Syscall numbers (must match libc/syscall.c).
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

// Phase 8b: signals
#define SYS_kill           90   // (pid, sig)
#define SYS_sigaction      91   // (sig, const struct sigaction *act, struct sigaction *oldact)
#define SYS_sigprocmask    92   // (how, const sigset_t *set, sigset_t *oldset)
#define SYS_sigreturn      93   // ()
#define SYS_pause          94   // ()

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
