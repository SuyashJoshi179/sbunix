#include <unistd.h>

// Generic 3-argument ecall helper
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
    // Round up to milliseconds (kernel resolution is ~10 ms anyway).
    return sleep_ms((us + 999UL) / 1000UL);
}
