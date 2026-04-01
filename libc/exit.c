#include <stdlib.h>
#include <syscall.h>

void exit(int status) {
    register unsigned long x10 asm("a0") = (unsigned long)status;
    register unsigned long x17 asm("a7") = SYS_exit;

    asm volatile("ecall"
                 : "+r"(x10)
                 : "r"(x17)
                 : "memory");

    while (1) {
    }
}
