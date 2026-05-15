#pragma once
#include <stdint.h>

typedef int32_t  pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t id_t;
typedef uint32_t mode_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint64_t blkcnt_t;
typedef uint64_t blksize_t;
typedef int64_t  time_t;
typedef int64_t  suseconds_t;
typedef int64_t  clock_t;
typedef int      clockid_t;
typedef void    *timer_t;
typedef uint32_t useconds_t;
typedef int64_t  off_t;
typedef int64_t  ssize_t;

/* size_t is also defined in <stddef.h>; share the guard so either
 * include order works. */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif
