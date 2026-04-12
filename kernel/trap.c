#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>
#include <proc.h>
#include <vmem.h>
#include <syscall.h>

extern int64_t syscall_handler(uint64_t num,
                                uint64_t a0, uint64_t a1, uint64_t a2);

/*
 * Saved register frame layout (trap.S saves x1..x31 in order at sp).
 *
 *   offset  0 = x1  (ra)
 *   offset  8 = x2  (sp)   ...
 *   offset 72 = x10 (a0)   → tf[9]
 *   offset 80 = x11 (a1)   → tf[10]
 *   offset 88 = x12 (a2)   → tf[11]
 *   offset 96 = x13 (a3)   → tf[12]
 *   offset104 = x14 (a4)   → tf[13]
 *   offset112 = x15 (a5)   → tf[14]
 *   offset128 = x17 (a7)   → tf[16]   ← syscall number
 */
#define TF_A0  9
#define TF_A1  10
#define TF_A2  11
#define TF_A3  12
#define TF_A4  13
#define TF_A5  14
#define TF_A7  16

static int64_t syscall_dispatch(uint64_t *tf) {
    return syscall_handler(tf[TF_A7],
                           tf[TF_A0], tf[TF_A1], tf[TF_A2]);
}

void trap_init(void) {
    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    write_sie(read_sie() | SIE_STIE);
    write_sstatus(read_sstatus() | SSTATUS_SIE | SSTATUS_SUM);
}

/* Restore user page table before sret returns to user mode.
   The user page table still maps all kernel addresses (upper half
   is copied from kernel_pgtable in create_user_pgtable), so the
   trap.S epilogue runs fine after this switch. */
static void restore_user_pgtable(void) {
    struct pcb *p = get_current();
    if (p && p->is_user && p->pagetable) {
        write_satp(make_satp(p->pagetable));
        flush_tlb();
    }
}

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t *tf) {
    write_satp(make_satp(kernel_pgtable));
    flush_tlb();

    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code   = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5:
                timer_handler();
                break;
            default:
                printk("Unknown interrupt: %ld\n", cause_code);
                while (1) {}
        }
    } else {
        if (cause_code == 8) {
            /* U-mode ecall — advance past the ecall instruction */
            write_sepc(sepc + 4);
            int64_t ret = syscall_dispatch(tf);
            tf[TF_A0] = (uint64_t)ret;
            restore_user_pgtable();
            return;
        }
        printk("Exception: scause=%lx, sepc=%lx, stval=%lx\n",
               scause, sepc, stval);
        while (1) {}
    }

    restore_user_pgtable();
}
