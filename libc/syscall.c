#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <limits.h>
#include "syscall_priv.h"

// Generic ecall helpers (register-allocated per RISC-V calling convention).
static long ecall1(long num, long a0) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}

static long ecall2(long num, long a0, long a1) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1) : "memory");
    return _a0;
}

static long ecall3(long num, long a0, long a1, long a2) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7), "r"(_a1), "r"(_a2) : "memory");
    return _a0;
}

long write(int fd, const void *buf, long len) {
    return syscall_ret(ecall3(2, (long)fd, (long)buf, len));
}

long read(int fd, void *buf, long len) {
    return syscall_ret(ecall3(5, (long)fd, (long)buf, len));
}

int open(const char *path, int flags, ...) {
    return (int)syscall_ret(ecall2(4, (long)path, (long)flags));
}

int close(int fd) {
    return (int)syscall_ret(ecall1(6, (long)fd));
}

int getpid(void) {
    return (int)syscall_ret(ecall3(8, 0, 0, 0));
}

int fork(void) {
    return (int)syscall_ret(ecall3(9, 0, 0, 0));
}

int wait(int *status) {
    return (int)syscall_ret(ecall3(7, (long)status, 0, 0));
}

int getppid(void) {
    return (int)syscall_ret(ecall3(11, 0, 0, 0));
}

int sched_yield(void) {
    return (int)syscall_ret(ecall3(12, 0, 0, 0));
}

int sleep_ms(unsigned long ms) {
    return (int)syscall_ret(ecall3(13, (long)ms, 0, 0));
}

int usleep(unsigned long us) {
    return sleep_ms((us + 999UL) / 1000UL);
}

int dup(int fd) {
    return (int)syscall_ret(ecall1(14, (long)fd));
}

int dup2(int oldfd, int newfd) {
    return (int)syscall_ret(ecall2(15, (long)oldfd, (long)newfd));
}

long lseek(int fd, long off, int whence) {
    return syscall_ret(ecall3(16, (long)fd, off, (long)whence));
}

int fstat(int fd, struct stat *st) {
    if (st) memset(st, 0, sizeof(*st));   /* kernel writes the fields it knows; the rest stays zero */
    return (int)syscall_ret(ecall2(17, (long)fd, (long)st));
}

int lstat(const char *path, struct stat *st) {
    if (st) memset(st, 0, sizeof(*st));
    return (int)syscall_ret(ecall2(113, (long)path, (long)st));
}

long getdents64(int fd, void *buf, long n) {
    return syscall_ret(ecall3(18, (long)fd, (long)buf, n));
}

int chdir(const char *path) {
    return (int)syscall_ret(ecall1(19, (long)path));
}

int mount(const char *target, const char *fstype) {
    return (int)syscall_ret(ecall2(83, (long)target, (long)fstype));
}

/* alarm(): kernel returns prior remaining seconds (always >= 0). errno
 * is not set by this syscall — POSIX promises no failure modes for
 * alarm(). We bypass syscall_ret since negatives are not errors here. */
unsigned alarm(unsigned secs) {
    return (unsigned)ecall1(84, (long)secs);
}

int truncate(const char *path, long length) {
    return (int)syscall_ret(ecall2(85, (long)path, length));
}

int ftruncate(int fd, long length) {
    return (int)syscall_ret(ecall2(86, (long)fd, length));
}

/* POSIX getcwd: returns buf on success, NULL on error.
 * glibc extension: buf == NULL → allocate. With size==0 use PATH_MAX.
 * BusyBox ash relies on this extension (`getcwd(NULL, 0)`). */
char *getcwd(char *buf, size_t n) {
    int allocated = 0;
    if (!buf) {
        if (n == 0) n = PATH_MAX;
        buf = malloc(n);
        if (!buf) { errno = ENOMEM; return 0; }
        allocated = 1;
    }
    long r = ecall2(20, (long)buf, (long)n);
    if (r < 0) {
        if (allocated) free(buf);
        errno = (int)-r;
        return 0;
    }
    return buf;
}

int mkdir(const char *path, int mode) {
    (void)mode;
    return (int)syscall_ret(ecall1(21, (long)path));
}

int unlink(const char *path) {
    return (int)syscall_ret(ecall1(22, (long)path));
}

int link(const char *oldpath, const char *newpath) {
    return (int)syscall_ret(ecall2(25, (long)oldpath, (long)newpath));
}

int rename(const char *oldpath, const char *newpath) {
    return (int)syscall_ret(ecall2(26, (long)oldpath, (long)newpath));
}

int pipe(int fds[2]) {
    return (int)syscall_ret(ecall1(23, (long)fds));
}

int execv(const char *path, char *const argv[]) {
    return (int)syscall_ret(ecall2(24, (long)path, (long)argv));
}

void *sbrk(long incr) {
    long r = ecall1(70, incr);
    if (r < 0 && r > -4096) {
        errno = (int)(-r);
        return (void *)-1;
    }
    return (void *)r;
}

static long ecall6(long num, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = a0;
    register long _a1 asm("a1") = a1;
    register long _a2 asm("a2") = a2;
    register long _a3 asm("a3") = a3;
    register long _a4 asm("a4") = a4;
    register long _a5 asm("a5") = a5;
    asm volatile("ecall"
        : "+r"(_a0)
        : "r"(_a7), "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5)
        : "memory");
    return _a0;
}

void *mmap(void *addr, long len, int prot, int flags, int fd, long off) {
    long r = ecall6(71, (long)addr, len, (long)prot, (long)flags, (long)fd, off);
    if (r < 0 && r > -4096) {
        errno = (int)(-r);
        return MAP_FAILED;
    }
    return (void *)r;
}

int munmap(void *addr, long len) {
    return (int)syscall_ret(ecall2(72, (long)addr, len));
}

int msync(void *addr, long len, int flags) {
    return (int)syscall_ret(ecall3(115, (long)addr, len, (long)flags));
}

int getrlimit(int resource, struct rlimit *rlim) {
    return (int)syscall_ret(ecall2(117, (long)resource, (long)rlim));
}

int setrlimit(int resource, const struct rlimit *rlim) {
    return (int)syscall_ret(ecall2(118, (long)resource, (long)rlim));
}

int ioctl(int fd, int cmd, void *arg) {
    return (int)syscall_ret(ecall3(110, (long)fd, (long)cmd, (long)arg));
}
long meminfo(void) {
    return ecall1(111, 0);
}

int isatty(int fd) {
    struct termios t;
    int r = ioctl(fd, TCGETS, &t);
    return r == 0;
}

int access(const char *path, int mode) {
    (void)path; (void)mode;
    errno = ENOSYS;
    return -1;
}

long readlink(const char *path, char *buf, long n) {
    return syscall_ret(ecall3(112, (long)path, (long)buf, n));
}

int wait4(int pid, int *status, int options, void *rusage) {
    (void)rusage;   /* kernel ignores it too */
    return (int)syscall_ret(ecall3(106, (long)pid, (long)status, (long)options));
}

int waitpid(int pid, int *status, int options) {
    return wait4(pid, status, options, 0);
}
