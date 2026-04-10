#include <stdlib.h>

void exit(int status) {
    register long _a7 asm("a7") = 1;   // SYS_exit
    register long _a0 asm("a0") = status;
    asm volatile("ecall" : : "r"(_a7), "r"(_a0) : "memory");
    __builtin_unreachable();
}
