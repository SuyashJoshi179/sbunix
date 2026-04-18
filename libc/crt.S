.global _start
_start:
    # Ensure gp is initialized for small-data/global accesses.
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop

    ld   a0, 0(sp)      # argc
    addi a1, sp, 8      # argv = sp + 8
    call main
    # a0 = main's return value, use as exit status
    li   a7, 1          # SYS_exit = 1
    ecall
1:  j 1b                # unreachable
