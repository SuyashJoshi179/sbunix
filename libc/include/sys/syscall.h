#pragma once

/* Stable syscall numbers visible to userspace. Numeric values must match
 * kernel/include/syscall.h (formatting/ordering may differ); the kernel
 * header carries the canonical definitions and a matching note. */

#define SYS_exit           1
#define SYS_write          2
#define SYS_exec           3
#define SYS_open           4
#define SYS_read           5
#define SYS_close          6
#define SYS_wait           7
#define SYS_getpid         8
#define SYS_fork           9
#define SYS_spawn         10
#define SYS_getppid       11
#define SYS_yield         12
#define SYS_sleep         13   /* arg0: milliseconds */
#define SYS_dup           14
#define SYS_dup2          15
#define SYS_lseek         16
#define SYS_fstat         17
#define SYS_getdents64    18
#define SYS_chdir         19
#define SYS_getcwd        20
#define SYS_mkdir         21
#define SYS_unlink        22
#define SYS_pipe          23
#define SYS_execv         24
#define SYS_link          25
#define SYS_rename        26
#define SYS_sigsuspend    27
#define SYS_sigpending    28
#define SYS_killpg        29
#define SYS_sigaltstack   30
#define SYS_sbrk          70
#define SYS_mmap          71
#define SYS_munmap        72
#define SYS_clock_gettime 80
#define SYS_gettimeofday  81
#define SYS_nanosleep     82
#define SYS_mount         83
#define SYS_alarm         84
#define SYS_truncate      85
#define SYS_ftruncate     86
#define SYS_symlink       87
#define SYS_clock_settime 88
#define SYS_clock_getres  89
#define SYS_kill          90
#define SYS_sigaction     91
#define SYS_sigprocmask   92
#define SYS_sigreturn     93
#define SYS_pause         94
#define SYS_setpgid       95
#define SYS_getpgid       96
#define SYS_getpgrp       97
#define SYS_setsid        98
#define SYS_getsid        99
#define SYS_getuid       100
#define SYS_geteuid      101
#define SYS_getgid       102
#define SYS_getegid      103
#define SYS_setuid       104
#define SYS_setgid       105
#define SYS_wait4        106
#define SYS_ioctl        110
#define SYS_meminfo      111
#define SYS_readlink     112
#define SYS_lstat        113
#define SYS_access       114
#define SYS_msync        115
#define SYS_getrlimit    117
#define SYS_setrlimit    118
#define SYS_fcntl        119
#define SYS_pread        120
#define SYS_pwrite       121
#define SYS_execve       122
#define SYS_utimensat    123
#define SYS_openat       124
#define SYS_stat         125
#define SYS_fstatat      126
#define SYS_unlinkat     127
#define SYS_mkdirat      128
#define SYS_fchdir       129
#define SYS_linkat       130
#define SYS_renameat     131
#define SYS_symlinkat    132
#define SYS_readlinkat   133

/* Generic syscall dispatch. Implemented as an asm stub (libc/syscall.c)
 * that shuffles the LP64 variadic argument registers (a0 = num, a1..a6 =
 * args) into the kernel's ecall layout (a7 = num, a0..a5 = args) and
 * tail-jumps into the -errno → errno translator. Calls with fewer than
 * six arguments are safe: the stub never reads from a va_list, so unused
 * argument-register slots simply carry stale values that the kernel
 * ignores for syscalls taking fewer parameters. On failure returns -1
 * with errno set; otherwise returns the raw kernel return value (which
 * may legitimately be 0). */
long syscall(long num, ...);
