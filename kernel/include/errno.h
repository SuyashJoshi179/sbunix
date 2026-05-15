#pragma once

#define EPERM         1
#define ENOENT        2
#define EACCES       13
#define ENODEV       19
#define ESRCH         3
#define EINTR         4
#define EBADF         9
#define ECHILD       10
#define ENOMEM       12
#define EFAULT       14
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOTTY       25
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define ERANGE       34
#define ENAMETOOLONG 36
#define ENOSYS       38
#define EIO           5
#define EBUSY        16
#define EEXIST       17
#define EFBIG        27
#define ENOTEMPTY    39
#define EPIPE        32
#define ENOTSUP      95
#define ELOOP        40
#define EXDEV        18    /* cross-device link */
#define ENOEXEC       8    /* exec format error */
#define EAGAIN       11    /* resource temporarily unavailable */
#define E2BIG         7    /* argument list too long */

/* Kernel-internal sentinels (must never reach userspace). Translated to
 * a real errno (typically EINTR) or used to trigger syscall restart by
 * check_signals_after_syscall on the ecall return path. Out of the POSIX
 * errno range so a stray leak is immediately visible. */
#define ERESTARTSYS  512
