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
#define TF_A0       9
#define TF_A1      10
#define TF_A2      11
#define TF_A3      12
#define TF_A7      16
#define TF_SEPC    31
#define TF_SSTATUS 32

// Dispatch a syscall.  trapframe points to the trap frame on the kernel stack.
// Returns the value to place in a0 on return to user.
int64_t syscall_dispatch(uint64_t sysnum, uint64_t *trapframe);
