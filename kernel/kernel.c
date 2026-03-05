#include <printk.h>
#include <stdint.h>

/* External function declarations for VirtIO Block driver */
extern void virtio_blk_init(uintptr_t base);
extern void virtio_blk_read(uint64_t sector, void *buf);
extern void virtio_blk_write(uint64_t sector, void *buf);

/* Global base address for the discovered VirtIO block device */
uintptr_t virtio_blk_base = 0;

/**
 * Scans MMIO space to identify VirtIO devices, specifically looking for the Block Device (ID 2).
 */
void virtio_probe() {
    printk("Scanning VirtIO MMIO devices...\n");

    for (int i = 0; i < 8; i++) {
        uintptr_t base = 0x10001000UL + (i * 0x1000);
        
        volatile uint32_t *magic_ptr = (uint32_t *)(base + 0x000);
        volatile uint32_t *devid_ptr = (uint32_t *)(base + 0x008);

        /* Check for VirtIO Magic Value "virt" */
        if (*magic_ptr == 0x74726976) {
            uint32_t devid = *devid_ptr;
            
            printk("Found VirtIO device at %x, ID: %x\n", (unsigned int)base, devid);

            /* Device ID 2 is the Block Device */
            if (devid == 2) {
                virtio_blk_base = base;
                printk("--> Successfully located VirtIO-Blk at %x\n", (unsigned int)base);
                break; 
            }
        }
    }

    if (virtio_blk_base == 0) {
        printk("Error: Could not find VirtIO-Blk device!\n");
    }
}

/**
 * Main kernel boot sequence.
 */
void boot(void) {
    printk("Booting SBUnix...\n");
    virtio_probe();

    if (virtio_blk_base != 0) {
        virtio_blk_init(virtio_blk_base);
        
        /* Static buffer to avoid stack issues during DMA */
        static char test_buf[512] __attribute__((aligned(4096)));
        
        /* 1. Initial read (may be 0 or injected string) */
        virtio_blk_read(0, test_buf);
        printk("First read [0]: %x\n", (unsigned char)test_buf[0]);

        /* 2. Modify buffer content in memory */
        test_buf[0] = 0x66; // 'f'
        test_buf[1] = 0x77; // 'w'
        
        /* 3. Perform write operation back to disk */
        virtio_blk_write(0, test_buf);
        
        /* 4. Clear memory and read again to verify the write */
        test_buf[0] = 0; test_buf[1] = 0;
        virtio_blk_read(0, test_buf);
        
        printk("Second read after write [0,1]: %x %x\n", 
               (unsigned char)test_buf[0], (unsigned char)test_buf[1]);
    }

    /* Halt execution */
    while (1);
}