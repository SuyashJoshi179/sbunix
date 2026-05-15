#pragma once
#include <sys/types.h>

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED   0x10
#define MAP_ANON    0x20
#define MAP_ANONYMOUS MAP_ANON

#define MAP_FAILED ((void *)-1)

#ifndef MS_SYNC
#define MS_SYNC 0x4
#endif

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   munmap(void *addr, size_t len);
int   msync(void *addr, size_t len, int flags);

int   mlock(const void *addr, size_t len);
int   munlock(const void *addr, size_t len);
int   mlockall(int flags);
int   munlockall(void);
int   posix_madvise(void *addr, size_t len, int advice);

#define POSIX_MADV_NORMAL     0
#define POSIX_MADV_RANDOM     1
#define POSIX_MADV_SEQUENTIAL 2
#define POSIX_MADV_WILLNEED   3
#define POSIX_MADV_DONTNEED   4

#define MCL_CURRENT 1
#define MCL_FUTURE  2
