#pragma once
#include_next <unistd.h>
#include <time.h>

#define _POSIX_TIMERS 0
#define _SC_NGROUPS_MAX 3
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84

int execv(const char *path, char *const argv[]);
#ifndef HAVE_USLEEP
int usleep(useconds_t usec);
#endif
int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
int chroot(const char *path);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
int getpagesize(void);
int pipe2(int pipefd[2], int flags);
int fchdir(int fd);
int fsync(int fd);
int mknod(const char *path, mode_t mode, dev_t dev);
int getgroups(int size, gid_t list[]);
int getopt(int argc, char *const argv[], const char *optstring);
pid_t vfork(void);
int execvp(const char *file, char *const argv[]);
char *getenv(const char *name);

extern char *optarg;
extern int optind, opterr, optopt;

#define _CS_PATH 0
size_t confstr(int name, char *buf, size_t len);
#define UTIME_NOW ((1L << 30) - 1L)
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags);
