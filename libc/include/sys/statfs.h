#pragma once
#include <stdint.h>
#include <sys/types.h>

typedef struct { int val[2]; } fsid_t;

struct statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    fsid_t   f_fsid;
    uint64_t f_namelen;
    uint64_t f_frsize;
    uint64_t f_flags;
    uint64_t f_spare[4];
};

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    uint64_t      f_blocks;
    uint64_t      f_bfree;
    uint64_t      f_bavail;
    uint64_t      f_files;
    uint64_t      f_ffree;
    uint64_t      f_favail;
    unsigned long f_fsid;
    unsigned long f_flag;
    unsigned long f_namemax;
};

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);
int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);
