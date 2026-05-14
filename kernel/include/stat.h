#pragma once
#include <stdint.h>
#include <time.h>

/* POSIX struct stat. Layout must remain byte-identical between
 * kernel/include/stat.h and libc/include/sys/stat.h — the kernel writes
 * this struct directly into a user buffer via the stat/fstat/lstat
 * syscalls. The timestamps use struct timespec per POSIX-2008+. */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    struct timespec st_atim;   /* last access time          */
    struct timespec st_mtim;   /* last data modification    */
    struct timespec st_ctim;   /* last status change        */
    uint64_t st_rdev;       /* device id for char/block special files */
    uint64_t st_blksize;    /* preferred I/O block size */
    uint64_t st_blocks;     /* number of 512-byte blocks allocated */
};

/* Helper: set all three timestamps to the same seconds value, zero nsec.
 * Filesystems that don't yet track nanosecond precision use this. */
#define STAT_SET_TIMES(st, secs) do {                       \
    uint64_t _s = (secs);                                   \
    (st)->st_atim.tv_sec = _s; (st)->st_atim.tv_nsec = 0;   \
    (st)->st_mtim.tv_sec = _s; (st)->st_mtim.tv_nsec = 0;   \
    (st)->st_ctim.tv_sec = _s; (st)->st_ctim.tv_nsec = 0;   \
} while (0)

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFLNK  0120000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
