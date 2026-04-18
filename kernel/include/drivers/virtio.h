#pragma once
#include <stdint.h>

/* -----------------------------------------------------------------------
 * VirtIO MMIO register offsets (Version 2 / Modern spec, §4.2.2)
 * Physical base: 0x10001000 (QEMU virt, virtio-mmio-bus.0)
 * ----------------------------------------------------------------------- */
#define VIRTIO_MMIO_BASE             0x10001000UL
#define VIRTIO_MMIO_SIZE             0x1000UL

/* Register accessors — after vmem_init, add mem_offset to reach high-half VA */
extern unsigned long mem_offset;
#define VIRTIO_REG(off) \
    (*(volatile uint32_t *)((VIRTIO_MMIO_BASE + mem_offset) + (off)))

/* Read-only registers */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000  /* "virt" = 0x74726976 */
#define VIRTIO_MMIO_VERSION             0x004  /* 1=legacy, 2=modern  */
#define VIRTIO_MMIO_DEVICE_ID           0x008  /* 2 = block device    */
#define VIRTIO_MMIO_VENDOR_ID           0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034

/* Write-only registers */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0A0
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0A4

/* Read/write registers */
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060

/* Device status bits */
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

/* Magic value */
#define VIRTIO_MAGIC_VALUE              0x74726976UL  /* "virt" LE */

/* Feature bit: version 1 must be offered + accepted for modern MMIO */
#define VIRTIO_F_VERSION_1              32  /* bit index in 64-bit feature set */

/* Queue depth — 8 keeps the struct tiny; one request at a time is fine */
#define VIRTQ_SIZE                      8

/* -----------------------------------------------------------------------
 * On-wire structures (must match VirtIO spec exactly)
 * ----------------------------------------------------------------------- */

/* Virtqueue split-ring descriptor (16 bytes each) */
struct virtq_desc {
    uint64_t addr;    /* physical address of buffer              */
    uint32_t len;     /* length in bytes                         */
    uint16_t flags;   /* VIRTQ_DESC_F_* bitmask                  */
    uint16_t next;    /* next descriptor index (if NEXT set)     */
} __attribute__((packed));

/* Available ring (driver → device) */
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;  /* optional suppress-interrupt hint */
} __attribute__((packed));

/* Used ring element */
struct virtq_used_elem {
    uint32_t id;   /* head descriptor index of completed chain */
    uint32_t len;  /* bytes written by device (for reads)      */
} __attribute__((packed));

/* Used ring (device → driver) */
struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    uint16_t avail_event; /* optional suppress-interrupt hint */
} __attribute__((packed));

/* VirtIO block request header (16 bytes) */
struct virtio_blk_req {
    uint32_t type;      /* VIRTIO_BLK_T_IN or VIRTIO_BLK_T_OUT */
    uint32_t reserved;
    uint64_t sector;    /* 512-byte sector number               */
} __attribute__((packed));

/* -----------------------------------------------------------------------
 * Driver interface
 * ----------------------------------------------------------------------- */
void virtio_disk_init(void);
void virtio_disk_rw(uint32_t blockno, void *data, int write);
void virtio_disk_intr(void);  /* called from trap handler on IRQ 1 */
