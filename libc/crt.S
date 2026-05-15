.global _start
_start:
    # Ensure gp is initialized for small-data/global accesses.
    .option push
    .option norelax
    la   gp, __global_pointer$
    .option pop

    ld   a0, 0(sp)      # argc
    addi a1, sp, 8      # argv = sp + 8
    # envp follows argv's NULL terminator at sp + 8*(argc + 2):
    #   [argc] [argv[0]] ... [argv[argc-1]] [NULL] [envp[0]] ...
    addi t0, a0, 2
    slli t0, t0, 3
    add  a2, sp, t0
    call main
    # a0 = main's return value, use as exit status. Go through exit()
    # rather than SYS_exit directly so registered atexit handlers fire
    # when main returns (POSIX 7.1.1: a return from main is equivalent
    # to exit(retval)).
    call exit
1:  j 1b                # unreachable
