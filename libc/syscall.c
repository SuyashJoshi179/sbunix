#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdint.h>
#include <termios.h>
#include <errno.h>

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
    return ecall3(2, (long)fd, (long)buf, len);
}

long read(int fd, void *buf, long len) {
    return ecall3(5, (long)fd, (long)buf, len);
}

int open(const char *path, int flags) {
    return (int)ecall2(4, (long)path, (long)flags);
}

int close(int fd) {
    return (int)ecall1(6, (long)fd);
}

int getpid(void) {
    return (int)ecall3(8, 0, 0, 0);
}

int fork(void) {
    return (int)ecall3(9, 0, 0, 0);
}

int wait(int *status) {
    return (int)ecall3(7, (long)status, 0, 0);
}

int getppid(void) {
    return (int)ecall3(11, 0, 0, 0);
}

int sched_yield(void) {
    return (int)ecall3(12, 0, 0, 0);
}

int sleep_ms(unsigned long ms) {
    return (int)ecall3(13, (long)ms, 0, 0);
}

int usleep(unsigned long us) {
    return sleep_ms((us + 999UL) / 1000UL);
}

int dup(int fd) {
    return (int)ecall1(14, (long)fd);
}

int dup2(int oldfd, int newfd) {
    return (int)ecall2(15, (long)oldfd, (long)newfd);
}

long lseek(int fd, long off, int whence) {
    return ecall3(16, (long)fd, off, (long)whence);
}

int fstat(int fd, struct stat *st) {
    return (int)ecall2(17, (long)fd, (long)st);
}

int lstat(const char *path, struct stat *st) {
    return (int)ecall2(113, (long)path, (long)st);
}

long getdents64(int fd, void *buf, long n) {
    return ecall3(18, (long)fd, (long)buf, n);
}

int chdir(const char *path) {
    return (int)ecall1(19, (long)path);
}

long getcwd(char *buf, long n) {
    return ecall2(20, (long)buf, n);
}

int mkdir(const char *path, int mode) {
    (void)mode;
    return (int)ecall1(21, (long)path);
}

int unlink(const char *path) {
    return (int)ecall1(22, (long)path);
}

int link(const char *oldpath, const char *newpath) {
    return (int)ecall2(25, (long)oldpath, (long)newpath);
}

int pipe(int fds[2]) {
    return (int)ecall1(23, (long)fds);
}

int execv(const char *path, char *const argv[]) {
    return (int)ecall2(24, (long)path, (long)argv);
}

void *sbrk(long incr) {
    return (void *)ecall1(70, incr);
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
    return (void *)ecall6(71, (long)addr, len, (long)prot, (long)flags, (long)fd, off);
}

int munmap(void *addr, long len) {
    return (int)ecall2(72, (long)addr, len);
}

int ioctl(int fd, int cmd, void *arg) {
    return (int)ecall3(110, (long)fd, (long)cmd, (long)arg);
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
    return -ENOSYS;
}

long readlink(const char *path, char *buf, long n) {
    return ecall3(112, (long)path, (long)buf, n);
}

/* No SYS_waitpid in the kernel. Block on wait() and surface what we get;
 * options is ignored (WNOHANG is not supported — document this). pid==-1
 * matches wait()'s "any child"; any other pid is best-effort: we wait
 * once and verify the returned pid matches, otherwise return -ECHILD
 * because we can't push the unrelated child back onto the queue. */
int waitpid(int pid, int *status, int options) {
    (void)options;
    int got = wait(status);
    if (pid == -1 || pid == 0) return got;
    if (got == pid || got < 0) return got;
    return -ECHILD;
}
