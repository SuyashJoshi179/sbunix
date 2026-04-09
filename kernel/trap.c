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

// trapframe layout (offsets into the 288-byte frame on the kernel stack):
//   x1..x31 at offsets 0..240 (8 bytes each, x2 at 8)
//   sepc    at offset 248
//   sstatus at offset 256
#define TF_A0  9    // index of a0 (x10) in the frame word array
#define TF_A7  16   // index of a7 (x17)

void trap_handler(uint64_t scause, uint64_t sepc, uint64_t stval, uint64_t *trapframe) {
    uint64_t is_interrupt = scause & (1UL << 63);
    uint64_t cause_code   = scause & 0xFF;

    if (is_interrupt) {
        switch (cause_code) {
            case 5:
                timer_handler();
                break;
            default:
                printk("Unknown interrupt: cause=%ld sepc=%lx\n", cause_code, sepc);
                while (1) {}
        }
    } else {
        if (cause_code == 8) {
            // Environment call from U-mode (ecall) — syscall dispatch (Phase B)
            // Advance past the ecall instruction
            // trapframe[248/8] is sepc slot; write_sepc handles CSR update
            // For now, panic — will be replaced in Phase B.
            printk("ecall: syscall %ld from sepc=%lx (not yet implemented)\n",
                   trapframe[TF_A7], sepc);
            while (1) {}
        }

        printk("Exception: scause=%lx sepc=%lx stval=%lx\n", scause, sepc, stval);
        while (1) {}
    }
}

