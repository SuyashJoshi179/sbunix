#include <unistd.h>
#include <syscall.h>
#include <sys/wait.h>
#include <time.h>

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

static inline long do_syscall2(unsigned long n, unsigned long a0, unsigned long a1) {
    register unsigned long x10 asm("a0") = a0;
    register unsigned long x11 asm("a1") = a1;
    register unsigned long x17 asm("a7") = n;

    asm volatile("ecall"
                 : "+r"(x10)
                 : "r"(x11), "r"(x17)
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

ssize_t write(int fd, const void *buf, unsigned long count) {
    return (ssize_t)do_syscall3(SYS_write, (unsigned long)fd, (unsigned long)buf, count);
}

int exec(const char *path) {
    return (int)do_syscall1(SYS_exec, (unsigned long)path);
}

int open(const char *path) {
    return (int)do_syscall1(SYS_open, (unsigned long)path);
}

ssize_t read(int fd, void *buf, unsigned long count) {
    return (ssize_t)do_syscall3(SYS_read, (unsigned long)fd, (unsigned long)buf, count);
}

int close(int fd) {
    return (int)do_syscall1(SYS_close, (unsigned long)fd);
}

int wait(void) {
    return (int)do_syscall3(SYS_wait, (unsigned long)-1, 0, 0);
}

pid_t waitpid(pid_t pid, int *wstatus, int options) {
    return (pid_t)do_syscall3(SYS_wait, (unsigned long)pid, (unsigned long)wstatus, (unsigned long)options);
}

int spawn(const char *path) {
    return (int)do_syscall1(SYS_spawn, (unsigned long)path);
}

int getpid(void) {
    return (int)do_syscall0(SYS_getpid);
}

int kill(int pid, int sig) {
    return (int)do_syscall2(SYS_kill, (unsigned long)pid, (unsigned long)sig);
}

time_t time(time_t *tloc) {
    time_t t = (time_t)do_syscall1(SYS_time, (unsigned long)tloc);
    if (tloc) {
        *tloc = t;
    }
    return t;
}
