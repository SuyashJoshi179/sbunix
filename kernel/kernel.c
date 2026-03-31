#include <printk.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>

extern char _kernel_end[];
extern void vmem_switch_to_high(unsigned long satp, unsigned long offset);

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    vmem_switch_to_high(make_satp(kernel_pgtable), KVMEM_OFFSET);
    vmem_init_post();

    trap_init();
    timer_init();
    printk("Starting scheduler\n");
    sched_init();   // never returns
}