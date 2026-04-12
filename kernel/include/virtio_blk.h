#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdint.h>

// --- (Legacy) ---
#define VIRTIO_MMIO_MAGIC_VALUE    0x000
#define VIRTIO_MMIO_VERSION        0x004
#define VIRTIO_MMIO_DEVICE_ID      0x008
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_GUEST_FEATURES  0x020
#define VIRTIO_MMIO_QUEUE_SEL      0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX  0x034
#define VIRTIO_MMIO_QUEUE_NUM      0x038
#define VIRTIO_MMIO_QUEUE_PFN      0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY   0x050
#define VIRTIO_MMIO_STATUS         0x070

// --- status ---
#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8

// --- Virtqueue descriptor ---
#define VRING_DESC_F_NEXT          1 // has next field, chain continues
#define VRING_DESC_F_WRITE         2 // device writes (vs read-only)

// descriptor (16 bytes)
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// (Available Ring)
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[16];
} __attribute__((packed));

// (Used Ring)
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[16];
};

// header for block request (16 bytes)
struct virtio_blk_req {
    uint32_t type;     // 0 read，1 write
    uint32_t reserved;
    uint64_t sector;   // sector number (512 bytes per sector)
} __attribute__((packed));

/**
 * Initialize the virtio block device at the given MMIO base address.
 * Sets up the virtqueues and negotiates features.
 */
void virtio_blk_init(uintptr_t base);

/**
 * Read one 512-byte sector from the disk into the buffer.
 * @param sector: The logical sector number to read.
 * @param buf: Pointer to a 512-byte buffer (should be aligned).
 */
void virtio_blk_read(uint64_t sector, void *buf);

/**
 * Write 512 bytes from the buffer to a specific disk sector.
 * @param sector: The logical sector number to write to.
 * @param buf: Pointer to the data to be written.
 */
void virtio_blk_write(uint64_t sector, void *buf);

#endif