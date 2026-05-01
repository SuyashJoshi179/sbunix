#pragma once
#include <stdint.h>

typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t  time_t;
typedef int64_t  suseconds_t;
typedef int64_t  off_t;
typedef int64_t  ssize_t;     /* signed counterpart of size_t */
typedef uint32_t mode_t;      /* file mode bits */
typedef uint64_t dev_t;       /* device ID */
typedef uint64_t ino_t;       /* inode number */
typedef uint32_t nlink_t;     /* hard-link count */
typedef int64_t  clock_t;     /* CPU ticks */
typedef int64_t  blksize_t;   /* block size in bytes */
typedef int64_t  blkcnt_t;    /* number of blocks */

/* size_t is also defined in <stddef.h>; share the guard so either
 * include order works. */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif
