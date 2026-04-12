#include <virtio_blk.h>
#include <printk.h>
#include <vmem.h>

/*
 * virt_to_phys: dev branch vmem.h does not export this helper,
 * so we compute it directly using mem_offset (declared in vmem.h).
 * After vmem_switch_to_high, mem_offset == KVMEM_OFFSET, so
 * physical = kernel_virtual - mem_offset.
 */
static inline unsigned long vtop(unsigned long virt) {
    if (virt >= KVMEM_OFFSET)
        return virt - mem_offset;
    return virt;
}

/* Static allocation: 2 pages (8 KB) for virtqueue structures */
static char __attribute__((aligned(4096))) queue_mem[8192];

static struct virtq_desc  *desc;
static struct virtq_avail *avail;
static struct virtq_used  *used;

static uintptr_t blk_base;

void virtio_blk_init(uintptr_t base) {
    blk_base = base;

    uint32_t magic   = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_MAGIC_VALUE);
    uint32_t version = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_VERSION);
    uint32_t devid   = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_DEVICE_ID);
    printk("[virtio] base=%lx magic=0x%x version=%d devid=%d\n",
           base, magic, version, devid);

    /* 1. Reset */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) = 0;
    __sync_synchronize();
    printk("[virtio] after reset: status=0x%x\n",
           *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS));

    /* 2. ACKNOWLEDGE + DRIVER */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    __sync_synchronize();
    printk("[virtio] after ACK+DRV: status=0x%x\n",
           *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS));

    /* 3. Feature negotiation */
    uint32_t features = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_DEVICE_FEATURES);
    printk("[virtio] device_features=0x%x\n", features);
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_GUEST_FEATURES) = features;

    /* 4. Set GuestPageSize (Legacy v1 requirement) */
    *(volatile uint32_t *)(blk_base + 0x028) = 4096;

    /* 5. Select queue 0 and configure */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32_t qmax = *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NUM_MAX);
    printk("[virtio] queue_num_max=%d\n", qmax);
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NUM) = 16;

    /* 6. Virtqueue layout in queue_mem:
     *   [0 .. 255]  = 16 descriptors (16 * 16 bytes = 256 bytes)
     *   [256 .. ~]  = avail ring
     *   [4096 .. ]  = used ring (page-aligned, Legacy requirement) */
    desc  = (struct virtq_desc  *)queue_mem;
    avail = (struct virtq_avail *)(queue_mem + 16 * sizeof(struct virtq_desc));
    used  = (struct virtq_used  *)(queue_mem + 4096);

    avail->idx = 0;
    used->idx  = 0;

    /* Provide PFN (physical page frame number) to device */
    uint32_t pfn = (uint32_t)(vtop((unsigned long)queue_mem) >> 12);
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_PFN) = pfn;
    printk("[virtio] desc_pa=%lx avail_pa=%lx used_pa=%lx pfn=%lx\n",
           vtop((unsigned long)desc),
           vtop((unsigned long)avail),
           vtop((unsigned long)used),
           (unsigned long)pfn);
    printk("[virtio] PFN readback=0x%x\n",
           *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_PFN));

    /* 7. DRIVER_OK */
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();
    printk("[virtio] after DRIVER_OK: status=0x%x\n",
           *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_STATUS));
}

void virtio_blk_read(uint64_t sector, void *buf) {
    static struct virtio_blk_req req;
    static volatile uint8_t status;

    req.type   = 0;   /* VIRTIO_BLK_T_IN */
    req.sector = sector;
    status     = 0xFF;

    printk("virtio_read: sector %ld, buf_pa %lx\n",
           sector, vtop((unsigned long)buf));

    desc[0].addr  = vtop((unsigned long)&req);
    desc[0].len   = sizeof(struct virtio_blk_req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next  = 1;

    desc[1].addr  = vtop((unsigned long)buf);
    desc[1].len   = 512;
    desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    desc[1].next  = 2;

    desc[2].addr  = vtop((unsigned long)&status);
    desc[2].len   = 1;
    desc[2].flags = VRING_DESC_F_WRITE;

    avail->ring[avail->idx % 16] = 0;
    __sync_synchronize();
    avail->idx++;
    __sync_synchronize();
    printk("[virtio_read] avail->idx=%d used->idx=%d, notifying\n",
           avail->idx, used->idx);
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    while (used->idx != avail->idx)
        __sync_synchronize();

    if (status == 0)
        printk("Disk read finished successfully!\n");
    else
        printk("Disk read failed, status=0x%x\n", status);
}

void virtio_blk_write(uint64_t sector, void *buf) {
    static struct virtio_blk_req req;
    static volatile uint8_t status;

    req.type   = 1;   /* VIRTIO_BLK_T_OUT */
    req.sector = sector;
    status     = 0xFF;

    desc[0].addr  = vtop((unsigned long)&req);
    desc[0].len   = sizeof(struct virtio_blk_req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next  = 1;

    desc[1].addr  = vtop((unsigned long)buf);
    desc[1].len   = 512;
    desc[1].flags = VRING_DESC_F_NEXT;
    desc[1].next  = 2;

    desc[2].addr  = vtop((unsigned long)&status);
    desc[2].len   = 1;
    desc[2].flags = VRING_DESC_F_WRITE;

    avail->ring[avail->idx % 16] = 0;
    __sync_synchronize();
    avail->idx++;
    __sync_synchronize();
    *(volatile uint32_t *)(blk_base + VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    while (used->idx != avail->idx)
        __sync_synchronize();

    if (status == 0)
        printk("Disk write finished successfully!\n");
    else
        printk("Disk write failed, status=0x%x\n", status);
}
