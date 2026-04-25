/*
 * virtio_disk.c — VirtIO MMIO block device driver (polling mode)
 *
 * Supports both VirtIO MMIO Version 1 (legacy, as reported by QEMU 8.x with
 * virtio-blk-device,bus=virtio-mmio-bus.0) and Version 2 (modern).
 *
 * Legacy (v1) queue layout — single 2-page contiguous buffer, 4KB-aligned:
 *   page 0 [0..4095]:   descriptor table (VIRTQ_SIZE × 16) + available ring
 *   page 1 [4096..8191]: used ring
 *
 * Polling mode: after QueueNotify, spin until q_used->idx advances.
 */

#include <drivers/virtio.h>
#include <printk.h>
#include <vmem.h>
#include <string.h>
#include <riscv.h>

/* -----------------------------------------------------------------------
 * Legacy VirtIO MMIO additional registers (v1 only)
 * ----------------------------------------------------------------------- */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE  0x028  /* page size the guest uses      */
#define VIRTIO_MMIO_QUEUE_ALIGN      0x03C  /* used-ring alignment           */
#define VIRTIO_MMIO_QUEUE_PFN        0x040  /* page-frame # of queue memory  */

/* -----------------------------------------------------------------------
 * Queue memory
 *
 * Two contiguous 4 KB pages, 4 KB-aligned.  Layout:
 *   bytes [0 .. VIRTQ_SIZE*16-1]          : descriptor table
 *   bytes [VIRTQ_SIZE*16 .. 4095]         : available ring (fits easily)
 *   bytes [4096 .. 4096+sizeof(used)-1]   : used ring
 * ----------------------------------------------------------------------- */
#define Q_PAGE_SIZE  4096
#define Q_BUF_SIZE   (2 * Q_PAGE_SIZE)

static char q_buf[Q_BUF_SIZE] __attribute__((aligned(Q_PAGE_SIZE)));

#define Q_DESC_OFF   0
#define Q_AVAIL_OFF  (VIRTQ_SIZE * 16)           /* 8 * 16 = 128 bytes */
#define Q_USED_OFF   Q_PAGE_SIZE                 /* second page        */

#define Q_DESC   ((struct virtq_desc  *)(q_buf + Q_DESC_OFF))
#define Q_AVAIL  ((struct virtq_avail *)(q_buf + Q_AVAIL_OFF))
#define Q_USED   ((struct virtq_used  *)(q_buf + Q_USED_OFF))

/* Per-request header and status byte (one outstanding request at a time) */
static struct virtio_blk_req req_hdr  __attribute__((aligned(16)));
static volatile uint8_t      req_stat __attribute__((aligned(1)));

/* Set true once init succeeds; rw silently no-ops when false. Lets the
 * kernel boot to a shell on platforms that expose virtio over a
 * different transport (e.g. PCI), instead of spinning forever. */
static int disk_ready = 0;
int virtio_disk_ready(void) { return disk_ready; }

/* Driver's local copy of the last seen used->idx */
static uint16_t last_used_idx = 0;

/* -----------------------------------------------------------------------
 * virtio_disk_init — negotiate with the device and set up the virtqueue
 * ----------------------------------------------------------------------- */
void virtio_disk_init(void) {
    /* 1. Verify magic and device ID. */
    uint32_t magic = VIRTIO_REG(VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MAGIC_VALUE) {
        printk("virtio_disk: bad magic 0x%x\n", magic);
        return;
    }
    uint32_t devid = VIRTIO_REG(VIRTIO_MMIO_DEVICE_ID);
    if (devid != 2) {
        printk("virtio_disk: unexpected device ID %d (want 2=blk)\n", devid);
        return;
    }
    uint32_t ver = VIRTIO_REG(VIRTIO_MMIO_VERSION);

    /* 2. Reset the device. */
    VIRTIO_REG(VIRTIO_MMIO_STATUS) = 0;
    __sync_synchronize();

    /* 3. Acknowledge presence and declare ourselves as a driver. */
    VIRTIO_REG(VIRTIO_MMIO_STATUS) = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    __sync_synchronize();

    /* 4. Feature negotiation. */
    if (ver == 2) {
        /* Modern: negotiate VIRTIO_F_VERSION_1, then require FEATURES_OK. */
        VIRTIO_REG(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 0;
        VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 0;
        VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES)     = 0;
        VIRTIO_REG(VIRTIO_MMIO_DEVICE_FEATURES_SEL) = 1;
        VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES_SEL) = 1;
        VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = (1u << (VIRTIO_F_VERSION_1 - 32));
        VIRTIO_REG(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_FEATURES_OK;
        __sync_synchronize();
        if (!(VIRTIO_REG(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
            printk("virtio_disk: FEATURES_OK rejected\n");
            VIRTIO_REG(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_FAILED;
            return;
        }
    } else {
        /* Legacy (v1): no FEATURES_OK; accept no extra features. */
        VIRTIO_REG(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
        __sync_synchronize();
    }

    /* 5. Configure virtqueue 0. */
    VIRTIO_REG(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32_t qmax = VIRTIO_REG(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) {
        printk("virtio_disk: queue 0 not available\n");
        return;
    }
    uint32_t qsz = ((uint32_t)VIRTQ_SIZE < qmax) ? VIRTQ_SIZE : qmax;
    VIRTIO_REG(VIRTIO_MMIO_QUEUE_NUM) = qsz;

    /* Zero the queue memory. */
    memset(q_buf, 0, Q_BUF_SIZE);
    __sync_synchronize();

    /* 6. Tell the device where the queue lives. */
    if (ver == 2) {
        /* Modern split descriptors. */
        uint64_t desc_pa  = virt_to_phys((unsigned long)Q_DESC);
        uint64_t avail_pa = virt_to_phys((unsigned long)Q_AVAIL);
        uint64_t used_pa  = virt_to_phys((unsigned long)Q_USED);
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_DESC_LOW)   = (uint32_t)desc_pa;
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_DESC_HIGH)  = (uint32_t)(desc_pa  >> 32);
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_AVAIL_LOW)  = (uint32_t)avail_pa;
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_AVAIL_HIGH) = (uint32_t)(avail_pa >> 32);
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_USED_LOW)   = (uint32_t)used_pa;
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_USED_HIGH)  = (uint32_t)(used_pa  >> 32);
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_READY) = 1;
    } else {
        /* Legacy: single PFN covering both pages. */
        VIRTIO_REG(VIRTIO_MMIO_GUEST_PAGE_SIZE) = Q_PAGE_SIZE;
        __sync_synchronize();
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_ALIGN) = Q_PAGE_SIZE;
        uint64_t pa = virt_to_phys((unsigned long)q_buf);
        VIRTIO_REG(VIRTIO_MMIO_QUEUE_PFN) = (uint32_t)(pa >> 12);
    }
    __sync_synchronize();

    /* 7. Signal DRIVER_OK — device is now live. */
    VIRTIO_REG(VIRTIO_MMIO_STATUS) |= VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();

    {
        uint64_t pa = virt_to_phys((unsigned long)q_buf);
        printk("virtio_disk: init OK (ver=%d, qmax=%d, queue_pa=0x%lx)\n",
               ver, qmax, pa);
    }
    disk_ready = 1;
}

/* -----------------------------------------------------------------------
 * virtio_disk_rw — synchronous block read or write (polling)
 *
 * blockno : 512-byte block number
 * data    : kernel virtual address of a 512-byte buffer
 * write   : 0 = device→memory (read), 1 = memory→device (write)
 * ----------------------------------------------------------------------- */
void virtio_disk_rw(uint32_t blockno, void *data, int write) {
    if (!disk_ready) return;

    /* Disable interrupts for the duration. */
    uint64_t saved_sstatus = read_sstatus();
    write_sstatus(saved_sstatus & ~SSTATUS_SIE);

    /* Build the 3-descriptor chain ---------------------------------------- */

    req_hdr.type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req_hdr.reserved = 0;
    req_hdr.sector   = blockno;
    req_stat         = 0xFF;   /* sentinel; device overwrites with result */

    uint64_t hdr_pa  = virt_to_phys((unsigned long)&req_hdr);
    uint64_t data_pa = virt_to_phys((unsigned long)data);
    uint64_t stat_pa = virt_to_phys((unsigned long)&req_stat);

    /* Desc 0: request header (driver → device, device reads it) */
    Q_DESC[0].addr  = hdr_pa;
    Q_DESC[0].len   = sizeof(struct virtio_blk_req);
    Q_DESC[0].flags = VIRTQ_DESC_F_NEXT;
    Q_DESC[0].next  = 1;

    /* Desc 1: data buffer (READ → device writes; WRITE → device reads) */
    Q_DESC[1].addr  = data_pa;
    Q_DESC[1].len   = 512;
    Q_DESC[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    Q_DESC[1].next  = 2;

    /* Desc 2: status byte (device always writes back) */
    Q_DESC[2].addr  = stat_pa;
    Q_DESC[2].len   = 1;
    Q_DESC[2].flags = VIRTQ_DESC_F_WRITE;
    Q_DESC[2].next  = 0;

    /* Put head (desc 0) into the available ring and bump avail->idx -------- */
    uint16_t avail_slot    = Q_AVAIL->idx % VIRTQ_SIZE;
    Q_AVAIL->ring[avail_slot] = 0;   /* head is always descriptor 0 */
    __sync_synchronize();
    Q_AVAIL->idx++;
    __sync_synchronize();

    /* Kick the device (notify queue 0) */
    VIRTIO_REG(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    /* Poll until the device advances used->idx ----------------------------- */
    uint32_t spin = 0;
    while (Q_USED->idx == last_used_idx) {
        __sync_synchronize();
        if (++spin == 10000000) {
            printk("virtio_disk_rw: TIMEOUT waiting for used ring (used=%u last=%u)\n",
                   (uint32_t)Q_USED->idx, (uint32_t)last_used_idx);
            spin = 0;
        }
    }

    last_used_idx = Q_USED->idx;
    __sync_synchronize();

    if (req_stat != VIRTIO_BLK_S_OK)
        printk("virtio_disk: I/O error block %u (status=%d)\n",
               blockno, (int)req_stat);

    /* Restore interrupt enable state. */
    write_sstatus(saved_sstatus);
}

/* -----------------------------------------------------------------------
 * virtio_disk_intr — called from trap handler on IRQ 1.
 * Polling mode: just ACK the interrupt; the RW loop detects completion.
 * ----------------------------------------------------------------------- */
void virtio_disk_intr(void) {
    uint32_t isr = VIRTIO_REG(VIRTIO_MMIO_INTERRUPT_STATUS);
    VIRTIO_REG(VIRTIO_MMIO_INTERRUPT_ACK) = isr;
}
