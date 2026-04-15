#include <printk.h>
#include <pmem.h>
#include <vmem.h>
#include <riscv.h>
#include <sbi.h>
#include <trap.h>
#include <timer.h>
#include <proc.h>
#include <selftest.h>
#include <tarfs.h>
#include <vfs.h>
#include <bio.h>
#include <sbfs.h>
#include <drivers/uart.h>
#include <drivers/plic.h>
#include <drivers/virtio.h>

// devfs_init declared here to avoid a new header for one function.
void devfs_init(void);

extern char _kernel_end[];

void boot(unsigned long hartid, unsigned long dtb_addr) {
    printk("Booting SBUnix\n");

    pmem_init(_kernel_end, (void *)PHYMEM_END);
    vmem_init();

    // Build tarfs inode tree and mount at "/".
    tarfs_init();

    // Set up PLIC and enable UART + VirtIO interrupts.
    plic_init();
    uart_init();

    // Mount synthetic /dev with /dev/console.
    devfs_init();

    // Phase 5: initialise buffer cache and VirtIO block device.
    binit();
    virtio_disk_init();

    // Mount sbfs at /data.
    struct inode *sbfs_root = sbfs_mount();
    if (sbfs_root) {
        int rc = mount_fs("/data", sbfs_root);
        if (rc < 0) {
            printk("kernel: mount /data failed (%d)\n", rc);
            inode_put(sbfs_root);   /* mount failed; release our ref */
        } else {
            printk("kernel: /data mounted (sbfs v1)\n");
            /* mount_child holds the ref — do NOT inode_put here */
        }
    } else {
        printk("kernel: sbfs_mount failed — /data unavailable\n");
    }

    trap_init();
    timer_init();

    selftest_run();

    printk("Starting scheduler\n");
    sched_init();   // never returns
}
