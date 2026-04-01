.section .text
.global enter_user

# void enter_user(unsigned long satp, unsigned long sepc, unsigned long usp, unsigned long ksp)
# a0 = satp, a1 = user pc, a2 = user sp, a3 = kernel sp for next trap
enter_user:
    csrw satp, a0
    sfence.vma
    csrw sepc, a1
    csrw sscratch, a3
    mv sp, a2
    sret
