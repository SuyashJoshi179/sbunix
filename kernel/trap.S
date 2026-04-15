.section .text
.global trap_vector
.align 2

trap_vector:
    # If trap came from U-mode (SPP=0 in sstatus), switch to the per-process
    # kernel stack stored in sscratch. For S-mode traps keep the current sp.
    csrr t0, sstatus
    andi t0, t0, 0x100      # SPP bit (bit 8)
    bnez t0, 1f             # skip swap if from S-mode
    csrrw sp, sscratch, sp  # swap: sp <-> sscratch (kernel stack)

1:
    # Push trap frame (288 bytes: 31 GPRs + sepc + sstatus + trapframe ptr slot)
    addi sp, sp, -288

    # Save all general-purpose registers (x2/sp saved via sscratch below)
    sd x1,   0(sp)
    # x2 (sp) — saved separately below
    sd x3,  16(sp)
    sd x4,  24(sp)
    sd x5,  32(sp)
    sd x6,  40(sp)
    sd x7,  48(sp)
    sd x8,  56(sp)
    sd x9,  64(sp)
    sd x10, 72(sp)
    sd x11, 80(sp)
    sd x12, 88(sp)
    sd x13, 96(sp)
    sd x14, 104(sp)
    sd x15, 112(sp)
    sd x16, 120(sp)
    sd x17, 128(sp)
    sd x18, 136(sp)
    sd x19, 144(sp)
    sd x20, 152(sp)
    sd x21, 160(sp)
    sd x22, 168(sp)
    sd x23, 176(sp)
    sd x24, 184(sp)
    sd x25, 192(sp)
    sd x26, 200(sp)
    sd x27, 208(sp)
    sd x28, 216(sp)
    sd x29, 224(sp)
    sd x30, 232(sp)
    sd x31, 240(sp)

    # Save sepc and sstatus so they survive across context switches
    csrr t0, sepc
    sd t0,  248(sp)
    csrr t0, sstatus
    sd t0,  256(sp)

    # Save the original sp (x2):
    # If we came from U-mode, sscratch now holds the user sp (we swapped above).
    # If we came from S-mode, sscratch is 0 (unused) and sp+288 is the real sp.
    csrr t0, sstatus
    andi t0, t0, 0x100      # SPP bit
    bnez t0, 2f             # S-mode: compute original sp
    csrr t0, sscratch       # U-mode: sscratch holds user sp
    j    3f
2:
    addi t0, sp, 288        # S-mode: original sp = current sp + frame size
3:
    sd t0, 8(sp)            # save original sp at offset 8 (x2 slot)

    # Call trap handler: trap_handler(scause, sepc, stval, trapframe)
    csrr a0, scause
    csrr a1, sepc
    csrr a2, stval
    mv   a3, sp             # pass trapframe pointer
    call trap_handler

    # Restore sepc and sstatus CSRs from the saved frame.
    ld t0, 248(sp)
    csrw sepc, t0
    ld t0, 256(sp)         # saved sstatus — also used for SPP check below
    csrw sstatus, t0

    # Branch on SPP *before* restoring GPRs (including t0/x5).
    # t0 still holds the saved sstatus value from the load above.
    # SPP=0 → came from U-mode, must swap sp<->sscratch on exit.
    andi t0, t0, 0x100     # isolate SPP bit
    beqz t0, .Lret_u

.Lret_s:
    # S-mode return: sp is already the kernel stack; no sscratch swap.
    ld x1,   0(sp)
    ld x3,  16(sp)
    ld x4,  24(sp)
    ld x5,  32(sp)
    ld x6,  40(sp)
    ld x7,  48(sp)
    ld x8,  56(sp)
    ld x9,  64(sp)
    ld x10, 72(sp)
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
    addi sp, sp, 288
    sret

.Lret_u:
    # U-mode return.  Restore all GPRs except x2/sp (handled below).
    ld x1,   0(sp)
    ld x3,  16(sp)
    ld x4,  24(sp)
    ld x5,  32(sp)
    ld x6,  40(sp)
    ld x7,  48(sp)
    ld x8,  56(sp)
    ld x9,  64(sp)
    ld x10, 72(sp)
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
    # Pop the frame: sp now equals kstack_top.
    addi sp, sp, 288
    # Write kstack_top back to sscratch so the NEXT U-mode trap entry can
    # swap stacks again.  We CANNOT use csrrw here: the scheduler may have
    # overwritten sscratch with kstack_top between the trap entry and now
    # (e.g., after a yield inside a syscall), so sscratch no longer holds
    # the original user_sp.  Instead, load user_sp from the trap frame
    # where trap.S saved it at entry (offset 8 = kstack_top - 280).
    csrw sscratch, sp       # sscratch = kstack_top
    ld   sp, -280(sp)       # sp = user_sp (saved at frame[1], offset 8)
    sret
