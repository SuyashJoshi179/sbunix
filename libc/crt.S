.global _start
_start:
    ld   a0, 0(sp)      # argc
    addi a1, sp, 8      # argv = sp + 8
    call main
    # a0 = main's return value, use as exit status
    li   a7, 1          # SYS_exit = 1
    ecall
1:  j 1b                # unreachable
