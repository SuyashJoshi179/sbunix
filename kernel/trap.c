#include <stdint.h>
#include <riscv.h>
#include <printk.h>
#include <timer.h>


void trap_init(void) {

    extern void trap_vector(void);
    write_stvec((uint64_t)trap_vector);
    write_sie(read_sie() | SIE_STIE);
    write_sstatus(read_sstatus() | SSTATUS_SIE);
}

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval) {
    
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5:
                timer_handler();
                break;
            default:
                printk("Unknown interrupt: %ld\n", cause_code);
                while (1) {}
                break;
        }
    }
    else {
        printk("Exception: scause=%lx, sepc=%lx, stval=%lx\n", scause, sepc, stval);
        while (1) {}
    }
}

