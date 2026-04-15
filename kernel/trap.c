#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>
#include <syscall.h>
#include <proc.h>

void trap_init(void) {
    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    write_sie(read_sie() | SIE_STIE);
    // Enable supervisor interrupts and allow S-mode to access U-mode pages
    // (SUM=1 is required so that syscall handlers can read user buffers)
    write_sstatus(read_sstatus() | SSTATUS_SIE | SSTATUS_SUM);
}

// Returns 1 if scause is a fault that kills a user process, 0 for kernel bugs.
static int is_user_fault(uint64_t cause_code) {
    switch (cause_code) {
        case 0:   // instruction address misaligned
        case 2:   // illegal instruction
        case 4:   // load address misaligned
        case 5:   // load access fault
        case 6:   // store/AMO address misaligned
        case 7:   // store/AMO access fault
        case 12:  // instruction page fault
        case 13:  // load page fault
        case 15:  // store/AMO page fault
            return 1;
        default:
            return 0;
    }
}

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t *trapframe) {
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code   = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5:
                timer_handler();
                return;
            default:
                printk("PANIC: unknown interrupt cause=%lu sepc=%lx\n",
                       cause_code, sepc);
                while (1) {}
        }
    }

    // Exception — check cause_code
    if (cause_code == 8) {
        // U-mode ecall: advance sepc past the ecall before dispatching so
        // that yield() inside a syscall resumes at the instruction after ecall.
        trapframe[TF_SEPC] += 4;
        int64_t ret = syscall_dispatch(trapframe[TF_A7], trapframe);
        trapframe[TF_A0] = (uint64_t)ret;
        return;
    }

    // For faults from U-mode, kill only the faulting process.
    // For faults from S-mode, it is a kernel bug — panic.
    int from_user = (trapframe[TF_SSTATUS] & SSTATUS_SPP) == 0;
    if (from_user && is_user_fault(cause_code)) {
        struct pcb *p = current_proc();
        printk("[trap] pid=%d killed by fault: scause=%lx sepc=%lx stval=%lx\n",
               p ? p->pid : -1, scause, sepc, stval);
        proc_exit_current(-14);  // SIGSEGV equivalent; never returns
    }

    printk("PANIC: kernel exception scause=%lx sepc=%lx stval=%lx\n",
           scause, sepc, stval);
    while (1) {}
}
