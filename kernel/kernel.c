#include <printk.h>
#include <drivers/uart.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>
#include <console.h>
#include <selftest.h>
#include <tarfs.h>
#include <drivers/uart.h>
#include <drivers/plic.h>

// devfs_init declared here to avoid a new header for one function.
void devfs_init(void);
#include <console.h>

extern char _kernel_end[];

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    // Build tarfs inode tree and mount at "/".
    tarfs_init();

    // Set up PLIC and enable UART RX interrupt.
    plic_init();
    uart_init();

    // Mount synthetic /dev with /dev/console.
    devfs_init();

    trap_init();
    timer_init();

    selftest_run();

    printk("Starting scheduler\n");
    sched_init();   // never returns
}
