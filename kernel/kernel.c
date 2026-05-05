#include <printk.h>
#include <page_ref.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>
#include <selftest.h>
#include <tarfs.h>
#include <procfs.h>
#include <vfs.h>
#include <bio.h>
#include <page_cache.h>
#include <sbfs.h>
#include <termios.h>
#include <drivers/uart.h>
#include <drivers/plic.h>
#include <drivers/pci.h>
#include <drivers/virtio.h>
#include <drivers/rtc.h>

// devfs_init declared here to avoid a new header for one function.
void devfs_init(void);

extern char _kernel_end[];

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();
    page_ref_init();

    // Build tarfs inode tree and mount at "/".
    tarfs_init();

    // Goldfish RTC: read once to anchor wall-clock time.
    rtc_init();

    // Set up PLIC and enable UART + VirtIO interrupts.
    plic_init();
    uart_init();

    // Mount synthetic /dev with /dev/console.
    devfs_init();
    procfs_init();
    termios_init();

    // Phase 5/10: buffer cache and VirtIO-PCI block device.
    binit();
    pcache_init();
    pci_init();
    virtio_disk_init();

    // Initialise sbfs in-memory state and replay the log. Attach is
    // deferred to userspace `mount -t disk … /mnt` (and to selftest's
    // internal pre-attach for kernel-side tests).
    if (sbfs_init() < 0)
        printk("kernel: sbfs_init failed — /mnt unavailable\n");

    trap_init();
    timer_init();

    selftest_run();

    printk("Starting scheduler\n");
    sched_init();   // never returns
}
