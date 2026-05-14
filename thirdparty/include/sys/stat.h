#pragma once
#include_next <sys/stat.h>

/* XSI sticky bit: required by busybox (libbb/mode_string.c hard-checks
 * S_ISVTX == 01000). Provide it if the student libc doesn't. */
#ifndef S_ISVTX
#define S_ISVTX 01000
#endif

int fstat(int fd, struct stat *st);
int chmod(const char *path, mode_t mode);
mode_t umask(mode_t mask);
int mknod(const char *path, mode_t mode, dev_t dev);
