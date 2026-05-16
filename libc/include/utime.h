#pragma once
#include <sys/types.h>

/* POSIX <utime.h>: legacy mtime/atime setter. The kernel implements the
 * superset via utimensat(2); utime() and utimes() are thin wrappers. */
struct utimbuf {
    time_t actime;
    time_t modtime;
};

int utime(const char *path, const struct utimbuf *buf);
