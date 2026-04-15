#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>

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

long getdents64(int fd, void *buf, long n) {
    return ecall3(18, (long)fd, (long)buf, n);
}

int chdir(const char *path) {
    return (int)ecall1(19, (long)path);
}

long getcwd(char *buf, long n) {
    return ecall2(20, (long)buf, n);
}
