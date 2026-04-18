#pragma once
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

long  write(int fd, const void *buf, long len);
long  read(int fd, void *buf, long len);
int   open(const char *path, int flags);
int   close(int fd);
int   getpid(void);
int   getppid(void);
int   fork(void);
int   wait(int *status);
int   sched_yield(void);
int   sleep_ms(unsigned long ms);
int   usleep(unsigned long us);
int   dup(int fd);
int   dup2(int oldfd, int newfd);
long  lseek(int fd, long off, int whence);
int   fstat(int fd, struct stat *st);
long  getdents64(int fd, void *buf, long n);
int   chdir(const char *path);
long  getcwd(char *buf, long n);
int   mkdir(const char *path, int mode);
int   unlink(const char *path);
int   pipe(int fds[2]);
int   execv(const char *path, char *const argv[]);
void *sbrk(long incr);

/* uid/gid stubs */
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int   setuid(uid_t uid);
int   setgid(gid_t gid);
