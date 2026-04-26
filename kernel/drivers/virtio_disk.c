/*
 * virtio_disk.c — virtio-blk over PCI (modern transport, polling mode).
 *
 * QEMU virt board: virtio-blk-pci-non-transitional, vendor 0x1AF4
 * device 0x1042. Transport structures live inside one BAR (typically
 * BAR4 on QEMU) and are advertised via VirtIO-vendor PCI capabilities.
 *
 * Completion: virtio_disk_rw polls the used ring. INTx via PLIC 32..35
 * is wired but only used as a wakeup hint — correctness does not depend
 * on it.
 */

#include <drivers/pci.h>
#include <drivers/virtio.h>
#include <printk.h>
#include <riscv.h>
#include <string.h>
#include <vmem.h>

#define VIRTIO_VENDOR_ID  0x1AF4
#define VIRTIO_BLK_DEVID  0x1042

/* -----------------------------------------------------------------------
 * Queue memory — one descriptor table + avail + used in a 4 KB-aligned
 * 8 KB region. Layout mirrors the previous MMIO driver so virtio_disk_rw
 * needs no rework beyond the kick.
 * ----------------------------------------------------------------------- */
#define Q_PAGE_SIZE 4096
#define Q_BUF_SIZE  (2 * Q_PAGE_SIZE)

static char q_buf[Q_BUF_SIZE] __attribute__((aligned(Q_PAGE_SIZE)));

#define Q_DESC_OFF  0
#define Q_AVAIL_OFF (VIRTQ_SIZE * 16)
#define Q_USED_OFF  Q_PAGE_SIZE

#define Q_DESC  ((struct virtq_desc  *)(q_buf + Q_DESC_OFF))
#define Q_AVAIL ((struct virtq_avail *)(q_buf + Q_AVAIL_OFF))
#define Q_USED  ((struct virtq_used  *)(q_buf + Q_USED_OFF))

static struct virtio_blk_req req_hdr  __attribute__((aligned(16)));
static volatile uint8_t      req_stat __attribute__((aligned(1)));
static uint16_t              last_used_idx = 0;

/* Resolved virtio-pci capability pointers (kernel virtual addresses). */
static volatile struct virtio_pci_common_cfg *common_cfg;
static volatile uint8_t  *isr_cfg;
static volatile uint16_t *notify_reg;
static int                disk_ready = 0;

int virtio_disk_ready(void) { return disk_ready; }

/* Resolve (bar, bar_off) → kernel virtual address. virtio-pci on QEMU
 * virt routes BARs into the 0x40000000+ window we already mapped. */
static volatile void *bar_resolve(uint8_t bus, uint8_t dev, uint8_t fn,
                                  uint8_t bar_idx, uint32_t bar_off) {
    uint64_t base = pci_read_bar(bus, dev, fn, bar_idx, 0);
    if (!base) return 0;
    return (volatile void *)(mem_offset + base + bar_off);
}

void virtio_disk_init(void) {
    uint8_t bus, dev, fn;
    if (!pci_find(VIRTIO_VENDOR_ID, VIRTIO_BLK_DEVID, &bus, &dev, &fn)) {
        printk("virtio_disk: no virtio-blk PCI device\n");
        return;
    }
    printk("virtio_disk: found at %x:%x.%x\n", bus, dev, fn);

    pci_enable(bus, dev, fn);

    /* Disable MSI-X so INTx is delivered. */
    uint8_t msix = pci_find_capability(bus, dev, fn, PCI_CAP_ID_MSIX, 0);
    if (msix) {
        uint16_t ctrl = pci_cfg_read16(bus, dev, fn, msix + 2);
        pci_cfg_write16(bus, dev, fn, msix + 2, ctrl & ~0x8000u);
    }

    /* Walk virtio-vendor caps and resolve the four config structures. */
    uint32_t notify_off_mult = 0;
    uint32_t notify_bar_off  = 0;
    uint8_t  notify_bar      = 0;
    uint8_t  cap = pci_find_capability(bus, dev, fn, PCI_CAP_ID_VENDOR, 0);
    while (cap) {
        uint8_t cfg_type = pci_cfg_read8 (bus, dev, fn, cap + 3);
        uint8_t bar      = pci_cfg_read8 (bus, dev, fn, cap + 4);
        uint32_t bar_off = pci_cfg_read32(bus, dev, fn, cap + 8);

        switch (cfg_type) {
        case VIRTIO_PCI_CAP_COMMON_CFG:
            common_cfg = bar_resolve(bus, dev, fn, bar, bar_off);
            break;
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            notify_bar       = bar;
            notify_bar_off   = bar_off;
            notify_off_mult  = pci_cfg_read32(bus, dev, fn, cap + 16);
            break;
        case VIRTIO_PCI_CAP_ISR_CFG:
            isr_cfg = bar_resolve(bus, dev, fn, bar, bar_off);
            break;
        }
        cap = pci_find_capability(bus, dev, fn, PCI_CAP_ID_VENDOR, cap);
    }

    if (!common_cfg || !isr_cfg) {
        printk("virtio_disk: missing virtio caps\n");
        return;
    }

    /* Reset, then ACK + DRIVER. */
    common_cfg->device_status = 0;
    __sync_synchronize();
    common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    __sync_synchronize();

    /* Negotiate VIRTIO_F_VERSION_1 (bit 32). */
    common_cfg->driver_feature_select = 0;
    common_cfg->driver_feature        = 0;
    common_cfg->driver_feature_select = 1;
    common_cfg->driver_feature        = 1u << (VIRTIO_F_VERSION_1 - 32);

    common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    __sync_synchronize();
    if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        printk("virtio_disk: FEATURES_OK rejected\n");
        common_cfg->device_status |= VIRTIO_STATUS_FAILED;
        return;
    }

    /* Configure virtqueue 0. */
    common_cfg->queue_select = 0;
    __sync_synchronize();
    uint16_t qmax = common_cfg->queue_size;
    if (qmax == 0) {
        printk("virtio_disk: queue 0 not available\n");
        return;
    }
    uint16_t qsz = ((uint16_t)VIRTQ_SIZE < qmax) ? VIRTQ_SIZE : qmax;
    common_cfg->queue_size = qsz;

    memset(q_buf, 0, Q_BUF_SIZE);
    __sync_synchronize();

    common_cfg->queue_desc   = virt_to_phys((unsigned long)Q_DESC);
    common_cfg->queue_driver = virt_to_phys((unsigned long)Q_AVAIL);
    common_cfg->queue_device = virt_to_phys((unsigned long)Q_USED);

    uint16_t notify_off = common_cfg->queue_notify_off;
    notify_reg = (volatile uint16_t *)bar_resolve(
        bus, dev, fn, notify_bar,
        notify_bar_off + (uint32_t)notify_off * notify_off_mult);

    common_cfg->queue_enable = 1;
    __sync_synchronize();

    common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    __sync_synchronize();

    disk_ready = 1;
    printk("virtio_disk: ready (queue_size=%d, notify_mult=%u)\n",
           qsz, notify_off_mult);
}

void virtio_disk_rw(uint32_t blockno, void *data, int write) {
    if (!disk_ready) return;

    uint64_t saved_sstatus = read_sstatus();
    write_sstatus(saved_sstatus & ~SSTATUS_SIE);

    req_hdr.type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req_hdr.reserved = 0;
    req_hdr.sector   = blockno;
    req_stat         = 0xFF;

    uint64_t hdr_pa  = virt_to_phys((unsigned long)&req_hdr);
    uint64_t data_pa = virt_to_phys((unsigned long)data);
    uint64_t stat_pa = virt_to_phys((unsigned long)&req_stat);

    Q_DESC[0].addr  = hdr_pa;
    Q_DESC[0].len   = sizeof(struct virtio_blk_req);
    Q_DESC[0].flags = VIRTQ_DESC_F_NEXT;
    Q_DESC[0].next  = 1;

    Q_DESC[1].addr  = data_pa;
    Q_DESC[1].len   = 512;
    Q_DESC[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    Q_DESC[1].next  = 2;

    Q_DESC[2].addr  = stat_pa;
    Q_DESC[2].len   = 1;
    Q_DESC[2].flags = VIRTQ_DESC_F_WRITE;
    Q_DESC[2].next  = 0;

    uint16_t avail_slot = Q_AVAIL->idx % VIRTQ_SIZE;
    Q_AVAIL->ring[avail_slot] = 0;
    __sync_synchronize();
    Q_AVAIL->idx++;
    __sync_synchronize();

    /* Kick: write queue index (0) into notify register. */
    *notify_reg = 0;

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

    write_sstatus(saved_sstatus);
}

/* ISR is read-to-clear: the read both fetches pending bits and ACKs them. */
void virtio_disk_intr(void) {
    if (isr_cfg) (void)*isr_cfg;
}
