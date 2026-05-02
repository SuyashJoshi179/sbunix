#include <unistd.h>
#include <errno.h>
#include "syscall_priv.h"

static long ecall0(long num) {
    register long _a7 asm("a7") = num;
    register long _a0 asm("a0") = 0;
    asm volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");
    return _a0;
}
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

pid_t getpgrp(void)        { return (pid_t)syscall_ret(ecall0(97)); }
pid_t getpgid(pid_t pid)   { return (pid_t)syscall_ret(ecall1(96, (long)pid)); }
int   setpgid(pid_t pid, pid_t pgid) {
    return (int)syscall_ret(ecall2(95, (long)pid, (long)pgid));
}
int   setpgrp(void)        { return setpgid(0, 0); }
pid_t setsid(void)         { return (pid_t)syscall_ret(ecall0(98)); }
pid_t getsid(pid_t pid)    { return (pid_t)syscall_ret(ecall1(99, (long)pid)); }

pid_t tcgetsid(int fd) {
    (void)fd;
    return getsid(0);
}
