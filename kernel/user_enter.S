.section .text
.global enter_user

# void enter_user(unsigned long satp, unsigned long user_entry,
#                 unsigned long user_sp,  unsigned long kernel_sp)
#
# Arguments (RISC-V calling convention):
#   a0 = satp value (SATP_SV39 | ppn of user page table root)
#   a1 = user entry point virtual address
#   a2 = user stack pointer
#   a3 = kernel stack top (stored in sscratch for trap.S to use on next trap)
#
# Drops to U-mode at user_entry with user_sp.  Never returns — user code
# eventually makes a SYS_exit ecall.
enter_user:
    csrw satp, a0           # switch to user page table
    sfence.vma zero, zero   # flush TLB after SATP change
    csrw sscratch, a3       # kernel sp so trap.S can swap stacks on U-mode trap
    csrw sepc, a1           # sret will jump here
    # sstatus: clear SPP (bit 8) → sret drops to U-mode
    #          set  SPIE (bit 5) → interrupts enabled in user after sret
    li   t0, (1 << 8)       # SSTATUS_SPP
    csrc sstatus, t0
    li   t0, (1 << 5)       # SSTATUS_SPIE
    csrs sstatus, t0
    mv   sp, a2             # set user stack pointer
    sret                    # jump to sepc in U-mode
