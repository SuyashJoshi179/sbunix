.global _start
_start:
    call main
    // main returned; call SYS_EXIT with return value (already in a0)
    li   a7, 1      // SYS_EXIT = 1
    ecall
    j .
