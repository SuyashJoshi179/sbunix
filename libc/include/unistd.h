#pragma once
#include <stdint.h>

/* syscall numbers — must match kernel/include/syscall.h */
#define SYS_EXIT  1
#define SYS_WRITE 2
#define SYS_READ  3
#define SYS_OPEN  4
#define SYS_CLOSE 5
#define SYS_LS    6

#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static inline long syscall(long num, long a0, long a1, long a2) {
    register long _num  asm("a7") = num;
    register long _a0   asm("a0") = a0;
    register long _a1   asm("a1") = a1;
    register long _a2   asm("a2") = a2;
    asm volatile("ecall"
        : "+r"(_a0)
        : "r"(_num), "r"(_a1), "r"(_a2)
        : "memory");
    return _a0;
}

static inline void exit(int status) {
    syscall(SYS_EXIT, status, 0, 0);
    while (1);
}

static inline int open(const char *path, int flags) {
    return (int)syscall(SYS_OPEN, (long)path, flags, 0);
}

static inline int read(int fd, void *buf, int n) {
    return (int)syscall(SYS_READ, fd, (long)buf, n);
}

static inline int write(int fd, const void *buf, int n) {
    return (int)syscall(SYS_WRITE, fd, (long)buf, n);
}

static inline int close(int fd) {
    return (int)syscall(SYS_CLOSE, fd, 0, 0);
}

static inline int ls(const char *path) {
    return (int)syscall(SYS_LS, (long)path, 0, 0);
}
