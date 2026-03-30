#include <printk.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");
    trap_init();
    timer_init();
    printk("Starting scheduler\n");
    sched_init();   // never returns
}