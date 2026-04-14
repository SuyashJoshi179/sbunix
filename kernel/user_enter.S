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

# ---------------------------------------------------------------------------
# fork_child_return — resume a forked child process back to user mode.
#
# Called by swtch() when the scheduler first runs a child created by fork().
# On entry:  sp = base of child's trap frame  (set by proc_fork_current)
#            sscratch = child kstack_top       (set by scheduler before swtch)
#
# The trap frame is a verbatim copy of the parent's frame at the moment the
# fork() syscall fired, except trapframe[TF_A0] was zeroed so fork() returns
# 0 in the child.  trapframe[TF_SEPC] already holds sepc+4 (trap_handler
# advanced it before calling syscall_dispatch).
#
# We restore sepc/sstatus and all GPRs from the frame, then load the user sp
# directly from trapframe[1] (offset 8) — the slot where trap.S stashed it
# during U-mode trap entry.  We do NOT use the csrrw-sscratch trick from
# trap.S because the scheduler has already set sscratch = kstack_top for the
# child's future traps.
# ---------------------------------------------------------------------------
.global fork_child_return
fork_child_return:
    ld   t0, 248(sp)
    csrw sepc, t0
    ld   t0, 256(sp)
    csrw sstatus, t0

    ld x1,   0(sp)
    # x2 (sp) is restored last
    ld x3,  16(sp)
    ld x4,  24(sp)
    ld x5,  32(sp)
    ld x6,  40(sp)
    ld x7,  48(sp)
    ld x8,  56(sp)
    ld x9,  64(sp)
    ld x10, 72(sp)    # a0 = 0 (fork returns 0 in child)
    ld x11, 80(sp)
    ld x12, 88(sp)
    ld x13, 96(sp)
    ld x14, 104(sp)
    ld x15, 112(sp)
    ld x16, 120(sp)
    ld x17, 128(sp)
    ld x18, 136(sp)
    ld x19, 144(sp)
    ld x20, 152(sp)
    ld x21, 160(sp)
    ld x22, 168(sp)
    ld x23, 176(sp)
    ld x24, 184(sp)
    ld x25, 192(sp)
    ld x26, 200(sp)
    ld x27, 208(sp)
    ld x28, 216(sp)
    ld x29, 224(sp)
    ld x30, 232(sp)
    ld x31, 240(sp)
    ld sp,    8(sp)   # restore user sp (must be last use of sp as base)
    sret
