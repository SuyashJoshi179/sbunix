#include <virtio_blk.h>
#include <printk.h>

/* Static allocation of 2 pages (8KB) for Virtqueue structures */
static char __attribute__((aligned(4096))) queue_mem[8192];
static struct virtq_desc *desc;
static struct virtq_avail *avail;
static struct virtq_used *used;

static uintptr_t blk_base;

/**
 * Initializes the VirtIO Block device using the Legacy MMIO interface.
 */
void virtio_blk_init(uintptr_t base) {
    blk_base = base;

    /* 1. Reset the device */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) = 0;
    __sync_synchronize();

    /* 2. Set ACKNOWLEDGE and DRIVER status bits */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_ACKNOWLEDGE;
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER;

    /* 3. Feature Negotiation: Enable VIRTIO_F_ANY_LAYOUT (bit 27) for Legacy stability */
    uint32_t features = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_DEVICE_FEATURES);
    features |= (1 << 27);
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_GUEST_FEATURES) = features;

    /* 4. Configure Virtqueue 0 */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_SEL) = 0;
    
    /* Set Guest Page Size (Legacy GuestPageSize register) */
    *(volatile uint32_t *)(blk_base + 0x028) = 4096;

    /* Set Queue Size to 16 descriptors */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NUM) = 16;

    /* 5. Memory Layout Setup */
    desc = (struct virtq_desc *)queue_mem;
    avail = (struct virtq_avail *)(queue_mem + 16 * sizeof(struct virtq_desc));
    /* Legacy mode requires Used Ring to be page-aligned (offset 4096) */
    used = (struct virtq_used *)(queue_mem + 4096); 

    /* Provide the Page Frame Number (PFN) to the device */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_PFN) = ((uintptr_t)queue_mem) >> 12;
    
    /* Initialize ring indices */
    avail->idx = 0;
    used->idx = 0;

    /* 6. Set DRIVER_OK to signal initialization completion */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();

    printk("VirtIO-Blk initialized at %x, PFN: %x\n", (unsigned int)blk_base, (unsigned int)((uintptr_t)queue_mem >> 12));
}

/**
 * Reads a 512-byte sector from the disk into the provided buffer.
 */
void virtio_blk_read(uint64_t sector, void *buf) {
    static struct virtio_blk_req req;
    static uint8_t status;

    req.type = 0; // VIRTIO_BLK_T_IN (Read)
    req.sector = sector;
    status = 0xFF; // Set non-zero initial status

    /* Descriptor 0: Request Header */
    desc[0].addr = (uintptr_t)&req;
    desc[0].len = sizeof(struct virtio_blk_req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    /* Descriptor 1: Data Buffer (Device writable) */
    desc[1].addr = (uintptr_t)buf;
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    desc[1].next = 2;

    /* Descriptor 2: Status Byte (Device writable) */
    desc[2].addr = (uintptr_t)&status;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;

    /* Add the head of the descriptor chain to the Avail Ring */
    avail->ring[avail->idx % 16] = 0;
    __sync_synchronize();
    
    /* Increment Avail index and notify the device */
    avail->idx++;
    __sync_synchronize();
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    /* Polling: Wait for the device to process the request */
    while (used->idx != avail->idx) {
        __sync_synchronize();
    }

    if (status == 0) {
        printk("Disk read finished successfully!\n");
    } else {
        printk("Disk read failed with status: %x\n", status);
    }
}

/**
 * Writes a 512-byte buffer to a specific sector on the disk.
 */
void virtio_blk_write(uint64_t sector, void *buf) {
    static struct virtio_blk_req req;
    static uint8_t status;

    req.type = 1; // VIRTIO_BLK_T_OUT (Write)
    req.sector = sector;
    status = 0xFF;

    /* Descriptor 0: Request Header */
    desc[0].addr = (uintptr_t)&req;
    desc[0].len = sizeof(struct virtio_blk_req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    /* Descriptor 1: Data Buffer (Device readable only) */
    desc[1].addr = (uintptr_t)buf;
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT; 
    desc[1].next = 2;

    /* Descriptor 2: Status Byte (Device writable) */
    desc[2].addr = (uintptr_t)&status;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;

    __sync_synchronize();
    avail->ring[avail->idx % 16] = 0; 
    __sync_synchronize();
    
    avail->idx++;
    __sync_synchronize();
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    /* Polling until completed */
    while (used->idx != avail->idx) {
        __sync_synchronize();
    }
    
    if (status == 0) {
        printk("Kernel write success!\n");
    } else {
        printk("Kernel write failed with status: %x\n", status);
    }
}