#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>
#include <syscall.h>

void trap_init(void) {
    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    write_sie(read_sie() | SIE_STIE);
    // Enable supervisor interrupts and allow S-mode to access U-mode pages
    // (SUM=1 is required so that syscall handlers can read user buffers)
    write_sstatus(read_sstatus() | SSTATUS_SIE | SSTATUS_SUM);
}

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t *trapframe) {
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code   = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5:
                timer_handler();
                break;
            default:
                printk("Unknown interrupt: cause=%lu sepc=%lx\n", cause_code, sepc);
                while (1) {}
        }
    } else {
        if (cause_code == 8) {
            // Environment call from U-mode — syscall
            // Advance sepc past the ecall instruction before the syscall
            // handler runs, so that a yield() inside the handler will
            // resume at the instruction after ecall on return.
            trapframe[TF_SEPC] += 4;

            int64_t ret = syscall_dispatch(trapframe[TF_A7], trapframe);
            trapframe[TF_A0] = (uint64_t)ret;
            return;
        }

        printk("Exception: scause=%lx sepc=%lx stval=%lx\n", scause, sepc, stval);
        while (1) {}
    }
}
