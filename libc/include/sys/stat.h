#pragma once
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* POSIX struct stat. Layout must match kernel/include/stat.h byte-for-byte
 * (the kernel writes this struct directly into a user buffer via the
 * stat/fstat/lstat syscalls). Timestamps use struct timespec per
 * POSIX-2008+; legacy bare-time_t names (st_atime/st_mtime/st_ctime) are
 * provided as macros for backward compatibility. */
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
    uint64_t st_rdev;
    uint64_t st_blksize;
    uint64_t st_blocks;
};

/* Back-compat aliases for POSIX-2001-era code that uses bare time_t names.
 * These work as rvalues only (st_atime, &st_atime); assignments must use
 * the new st_atim.tv_sec form. */
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISUID  04000
#define S_ISGID  02000
#define S_ISVTX  01000

#define S_IRWXU  0700
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXG  0070
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IRWXO  0007
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

int    fstat(int fd, struct stat *st);
int    stat(const char *path, struct stat *st);
int    lstat(const char *path, struct stat *st);
int    chmod(const char *path, mode_t mode);
int    fchmod(int fd, mode_t mode);
mode_t umask(mode_t mask);
int    mkfifo(const char *path, mode_t mode);
int    mknod(const char *path, mode_t mode, dev_t dev);
int    utimensat(int dirfd, const char *path,
                 const struct timespec times[2], int flags);
