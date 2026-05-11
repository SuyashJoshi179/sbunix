#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>
#include <syscall.h>
#include <proc.h>
#include <vma.h>
#include <signal.h>
#include <drivers/plic.h>
#include <drivers/uart.h>
#include <drivers/virtio.h>
#include <vmem.h>

#define SIE_SEIE  (1 << 9)   /* S-mode external interrupt enable */

/* Catch corrupted return-to-user PC before sret. User text+stack live below
 * USER_STACK_TOP (0x40000000). Anything at/above is a kernel bug. */
static void check_user_return(uint64_t *tf, const char *tag) {
    uint64_t pc = tf[TF_SEPC];
    if (pc >= USER_STACK_TOP) {
        struct pcb *p = current_proc();
        printk("[BUG %s] pid=%d returning to user with bad sepc=0x%lx — killing\n",
               tag, p ? p->pid : -1, pc);
        proc_exit_current(SIGSEGV & 0x7f);
    }
    uint64_t sp = tf[1];
    if (sp == 0 || sp >= USER_STACK_TOP) {
        struct pcb *p = current_proc();
        printk("[BUG %s] pid=%d returning to user with bad sp=0x%lx sepc=0x%lx — killing\n",
               tag, p ? p->pid : -1, sp, pc);
        proc_exit_current(SIGSEGV & 0x7f);
    }
}

void trap_init(void) {
    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    // Enable timer (STIE) and external (SEIE) interrupts in sie.
    write_sie(read_sie() | SIE_STIE | SIE_SEIE);
    // Enable supervisor interrupts and allow S-mode to access U-mode pages.
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
        int irq_from_user = (trapframe[TF_SSTATUS] & SSTATUS_SPP) == 0;
        switch (cause_code) {
            case 5:   /* supervisor timer interrupt */
                timer_handler();
                /* Preempt only when the timer fired in user mode. A timer
                 * trap raised while we were already executing kernel code
                 * (SPP=1) must not call yield(): that would let another
                 * process run mid-kernel-update, in violation of the
                 * "interrupts-off is the lock" model. */
                if (irq_from_user) {
                    yield();
                    check_signals(trapframe);
                    check_user_return(trapframe, "timer-ret");
                }
                return;
            case 9:   /* supervisor external interrupt (PLIC) */
            {
                int irq = plic_claim();
                if (irq == 10 /* UART_IRQ */) {
                    uart_rx_isr();
                } else if (irq >= 32 && irq <= 35 /* PCI INTA..INTD */) {
                    virtio_disk_intr();
                }
                if (irq) plic_complete(irq);
                if (irq_from_user) {
                    check_signals(trapframe);
                    check_user_return(trapframe, "extirq-ret");
                }
                return;
            }
            default:
                printk("PANIC: unknown interrupt cause=%lu sepc=%lx\n",
                       cause_code, sepc);
                while (1) {}
        }
    }

    // Exception — check cause_code
    int from_user = (trapframe[TF_SSTATUS] & SSTATUS_SPP) == 0;

    if (cause_code == 8) {
        // U-mode ecall: advance sepc past the ecall before dispatching so
        // that yield() inside a syscall resumes at the instruction after ecall.
        trapframe[TF_SEPC] += 4;
        int64_t ret = syscall_dispatch(trapframe[TF_A7], trapframe);
        trapframe[TF_A0] = (uint64_t)ret;
        check_signals(trapframe);
        check_user_return(trapframe, "ecall-ret");
        return;
    }

    // For faults from U-mode, kill only the faulting process.
    // For faults from S-mode, it is a kernel bug — panic.
    if (from_user && is_user_fault(cause_code)) {
        if (cause_code == 12 || cause_code == 13 || cause_code == 15) {
            if (user_page_fault(cause_code, stval, trapframe) == 0) {
                check_signals(trapframe);
                check_user_return(trapframe, "pf-ret");
                return;
            }
        }
        struct pcb *p = current_proc();
        printk("[trap] pid=%d killed by fault: scause=%lx sepc=%lx stval=%lx\n",
               p ? p->pid : -1, scause, sepc, stval);
        send_signal(p, SIGSEGV);
        check_signals(trapframe);
        return;
    }

    printk("PANIC: kernel exception scause=%lx sepc=%lx stval=%lx\n",
           scause, sepc, stval);
    while (1) {}
}
