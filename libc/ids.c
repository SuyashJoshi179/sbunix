#include <stdint.h>

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

unsigned int getuid(void)  { return (unsigned int)ecall0(100); }
unsigned int geteuid(void) { return (unsigned int)ecall0(101); }
unsigned int getgid(void)  { return (unsigned int)ecall0(102); }
unsigned int getegid(void) { return (unsigned int)ecall0(103); }
int setuid(unsigned int uid) { return (int)ecall1(104, (long)uid); }
int setgid(unsigned int gid) { return (int)ecall1(105, (long)gid); }
