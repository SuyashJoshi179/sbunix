#pragma once
#include <stdint.h>
#include <stddef.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* POSIX requires SEEK_SET/CUR/END to be visible via unistd.h (for lseek).
 * Keep values in lockstep with stdio.h. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

long  write(int fd, const void *buf, long len);
long  read(int fd, void *buf, long len);
int   open(const char *path, int flags, ...);
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
int   lstat(const char *path, struct stat *st);
long  getdents64(int fd, void *buf, long n);
int   chdir(const char *path);
char *getcwd(char *buf, size_t n);
int   mkdir(const char *path, int mode);
int   unlink(const char *path);
int   pipe(int fds[2]);
int   execv(const char *path, char *const argv[]);
int   execvp(const char *file, char *const argv[]);
int   execve(const char *path, char *const argv[], char *const envp[]);
int   execvpe(const char *file, char *const argv[], char *const envp[]);
int   execl(const char *path, const char *arg0, ...);
int   execlp(const char *file, const char *arg0, ...);
int   execle(const char *path, const char *arg0, ...);
extern char **environ;
void *sbrk(long incr);
long  meminfo(void);

/* process groups (stubs — kernel has no job control) */
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
int   setpgid(pid_t pid, pid_t pgid);
int   setpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t pid);
pid_t tcgetsid(int fd);

/* uid/gid stubs */
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int   setuid(uid_t uid);
int   setgid(gid_t gid);

void  _exit(int status) __attribute__((noreturn));
int   isatty(int fd);
int   access(const char *path, int mode);
long  readlink(const char *path, char *buf, long n);

int   gethostname(char *buf, size_t len);
int   sethostname(const char *name, size_t len);
int   getdomainname(char *buf, size_t len);
void  sync(void);
int   fsync(int fd);
int   fdatasync(int fd);
unsigned alarm(unsigned secs);
unsigned sleep(unsigned secs);
int   ftruncate(int fd, off_t len);
int   truncate(const char *path, off_t len);
int   chown(const char *path, uid_t uid, gid_t gid);
int   fchown(int fd, uid_t uid, gid_t gid);
int   lchown(const char *path, uid_t uid, gid_t gid);
int   link(const char *oldp, const char *newp);
int   symlink(const char *target, const char *linkp);
int   rmdir(const char *path);
char *ttyname(int fd);
int   ttyname_r(int fd, char *buf, size_t len);
long  pathconf(const char *path, int name);
long  fpathconf(int fd, int name);
long  sysconf(int name);

#define _SC_PAGESIZE          30
#define _SC_PAGE_SIZE         _SC_PAGESIZE
#define _SC_OPEN_MAX          4
#define _SC_NPROCESSORS_ONLN  84
#define _SC_NPROCESSORS_CONF  83
#define _SC_CLK_TCK           2

#define _PC_NAME_MAX          3
#define _PC_PATH_MAX          4

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
