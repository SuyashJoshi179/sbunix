.global _start
_start:
    li   a0, 0          # argc = 0
    li   a1, 0          # argv = NULL
    call main
    # a0 = main's return value, use as exit status
    li   a7, 1          # SYS_exit = 1
    ecall
1:  j 1b                # unreachable
