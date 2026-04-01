#include <printk.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>
#include <tarfs.h>

extern char _kernel_end[];
extern void vmem_switch_to_high(unsigned long satp, unsigned long offset);

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    vmem_switch_to_high(make_satp(kernel_pgtable), KVMEM_OFFSET);
    vmem_init_post();

    tarfs_init();
    struct tarfs_node echo_file;
    if (tarfs_lookup("/bin/echo", &echo_file)) {
        printk("tarfs: found /bin/echo (%lu bytes)\n", echo_file.size);
    } else {
        printk("tarfs: /bin/echo not found\n");
    }

    trap_init();
    timer_init();
    printk("Starting scheduler\n");
    sched_init();   // never returns
}