#pragma once

/* Stable syscall numbers visible to userspace. Must match
 * kernel/include/syscall.h byte-for-byte; the kernel header carries the
 * canonical definitions and a matching note. */

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
#define SYS_msync        115
#define SYS_getrlimit    117
#define SYS_setrlimit    118

/* Generic syscall dispatch. Always reads six argument slots from va_list
 * (slots the caller did not pass are read as indeterminate longs but
 * ignored by the kernel for syscalls that take fewer arguments). On
 * failure returns -1 with errno set; otherwise returns the raw kernel
 * return value (which may legitimately be 0). */
long syscall(long num, ...);
