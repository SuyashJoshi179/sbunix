#include <unistd.h>
#include <syscall.h>

static inline long do_syscall3(unsigned long n, unsigned long a0, unsigned long a1, unsigned long a2) {
    register unsigned long x10 asm("a0") = a0;
    register unsigned long x11 asm("a1") = a1;
    register unsigned long x12 asm("a2") = a2;
    register unsigned long x17 asm("a7") = n;

    asm volatile("ecall"
                 : "+r"(x10)
                 : "r"(x11), "r"(x12), "r"(x17)
                 : "memory");

    return (long)x10;
}

static inline long do_syscall1(unsigned long n, unsigned long a0) {
    register unsigned long x10 asm("a0") = a0;
    register unsigned long x17 asm("a7") = n;

    asm volatile("ecall"
                 : "+r"(x10)
                 : "r"(x17)
                 : "memory");

    return (long)x10;
}

static inline long do_syscall0(unsigned long n) {
    register unsigned long x10 asm("a0") = 0;
    register unsigned long x17 asm("a7") = n;

    asm volatile("ecall"
                 : "+r"(x10)
                 : "r"(x17)
                 : "memory");

    return (long)x10;
}

long write(int fd, const void *buf, unsigned long count) {
    return do_syscall3(SYS_write, (unsigned long)fd, (unsigned long)buf, count);
}

long exec(const char *path) {
    return do_syscall1(SYS_exec, (unsigned long)path);
}

long open(const char *path) {
    return do_syscall1(SYS_open, (unsigned long)path);
}

long read(int fd, void *buf, unsigned long count) {
    return do_syscall3(SYS_read, (unsigned long)fd, (unsigned long)buf, count);
}

long close(int fd) {
    return do_syscall1(SYS_close, (unsigned long)fd);
}

long wait(void) {
    return do_syscall0(SYS_wait);
}
