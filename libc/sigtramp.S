.global __sigtramp
__sigtramp:
    li a7, 93      # SYS_sigreturn
    ecall
    ebreak         # should never return
