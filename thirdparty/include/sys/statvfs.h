#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <sys/types.h>

typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;

struct statvfs {
	unsigned long f_bsize, f_frsize;
	fsblkcnt_t f_blocks, f_bfree, f_bavail;
	fsfilcnt_t f_files, f_ffree, f_favail;
	unsigned long f_fsid, f_flag, f_namemax;
};

static inline int statvfs(const char *path, struct statvfs *buf) { (void)path; (void)buf; return -1; }

#endif
