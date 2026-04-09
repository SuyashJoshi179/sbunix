#include <printk.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>

extern char _kernel_end[];

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    trap_init();
    timer_init();
    printk("Starting scheduler\n");
    sched_init();   // never returns
}
