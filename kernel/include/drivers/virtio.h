#pragma once
#include <stdint.h>

/* -----------------------------------------------------------------------
 * VirtIO over PCI (modern, spec ≥ 1.0)
 *
 * QEMU virt board exposes virtio-blk-pci-non-transitional with vendor
 * 0x1AF4, device 0x1042. The transport-level structures (COMMON, NOTIFY,
 * ISR, DEVICE) live inside one of the device's BARs and are advertised
 * via VirtIO-vendor PCI capabilities (cap_vndr = 0x09).
 *
 * Ring layout (split virtqueue) is identical across MMIO and PCI, so
 * the data-path structs below are unchanged.
 * ----------------------------------------------------------------------- */

/* virtio_pci_cap.cfg_type values (spec §4.1.4.3). */
#define VIRTIO_PCI_CAP_COMMON_CFG       1
#define VIRTIO_PCI_CAP_NOTIFY_CFG       2
#define VIRTIO_PCI_CAP_ISR_CFG          3
#define VIRTIO_PCI_CAP_DEVICE_CFG       4
#define VIRTIO_PCI_CAP_PCI_CFG          5

/* Layout of a virtio-vendor PCI capability (spec §4.1.4). */
struct virtio_pci_cap {
    uint8_t  cap_vndr;        /* 0x09 (PCI_CAP_ID_VENDOR) */
    uint8_t  cap_next;        /* next capability pointer */
    uint8_t  cap_len;         /* bytes (>= 16) */
    uint8_t  cfg_type;        /* VIRTIO_PCI_CAP_* */
    uint8_t  bar;             /* which BAR holds the structure */
    uint8_t  padding[3];
    uint32_t offset;          /* offset within BAR */
    uint32_t length;          /* length of the structure */
} __attribute__((packed));

/* Common configuration structure (spec §4.1.4.3). */
struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;

    /* Per-queue (selected via queue_select). */
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_driver;    /* avail */
    uint64_t queue_device;    /* used  */
};
/* Fields are naturally aligned; no packing — packed struct access can
 * be lowered to byte loads, which virtio-pci registers reject. */

/* Device status bits (shared with MMIO transport). */
#define VIRTIO_STATUS_ACKNOWLEDGE       (1 << 0)
#define VIRTIO_STATUS_DRIVER            (1 << 1)
#define VIRTIO_STATUS_DRIVER_OK         (1 << 2)
#define VIRTIO_STATUS_FEATURES_OK       (1 << 3)
#define VIRTIO_STATUS_NEEDS_RESET       (1 << 6)
#define VIRTIO_STATUS_FAILED            (1 << 7)

/* Virtqueue descriptor flags */
#define VIRTQ_DESC_F_NEXT               1   /* descriptor chains to next */
#define VIRTQ_DESC_F_WRITE              2   /* device writes to this buf */

/* Block request types */
#define VIRTIO_BLK_T_IN                 0   /* device → memory (read)   */
#define VIRTIO_BLK_T_OUT                1   /* memory → device (write)  */

/* Block request status */
#define VIRTIO_BLK_S_OK                 0
#define VIRTIO_BLK_S_IOERR              1
#define VIRTIO_BLK_S_UNSUPP             2

/* Feature bit: VIRTIO_F_VERSION_1 — required for modern devices. */
#define VIRTIO_F_VERSION_1              32

/* Queue depth — keep small; one request at a time is fine for sbfs. */
#define VIRTQ_SIZE                      8

/* -----------------------------------------------------------------------
 * On-wire ring structures (shared with MMIO transport — unchanged).
 * ----------------------------------------------------------------------- */

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

/* -----------------------------------------------------------------------
 * Driver interface
 * ----------------------------------------------------------------------- */
void virtio_disk_init(void);
void virtio_disk_rw(uint32_t blockno, void *data, int write);
void virtio_disk_intr(void);   /* PCI INTx — PLIC IRQ 32..35 */
int  virtio_disk_ready(void);  /* 1 once device negotiated */
