/* PCIe ECAM bus driver for QEMU virt board.
 * See kernel/include/drivers/pci.h for the public API. */
#include <drivers/pci.h>
#include <printk.h>
#include <vmem.h>

#define ECAM_PHYS  0x30000000UL
#define ECAM_SIZE  0x10000000UL

/* High-half VA of ECAM after vmem_init. mem_offset is set then. */
static inline volatile uint8_t *cfg_addr(uint8_t bus, uint8_t dev,
                                         uint8_t fn, uint16_t off) {
    unsigned long pa = ECAM_PHYS
                     + ((unsigned long)bus << 20)
                     + ((unsigned long)dev << 15)
                     + ((unsigned long)fn  << 12)
                     + off;
    return (volatile uint8_t *)(mem_offset + pa);
}

uint8_t pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off) {
    return *cfg_addr(bus, dev, fn, off);
}

uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off) {
    return *(volatile uint16_t *)cfg_addr(bus, dev, fn, off);
}

uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off) {
    return *(volatile uint32_t *)cfg_addr(bus, dev, fn, off);
}

void pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint16_t v) {
    *(volatile uint16_t *)cfg_addr(bus, dev, fn, off) = v;
}

void pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v) {
    *(volatile uint32_t *)cfg_addr(bus, dev, fn, off) = v;
}

void pci_enable(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint16_t cmd = pci_cfg_read16(bus, dev, fn, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY | PCI_CMD_MASTER;
    pci_cfg_write16(bus, dev, fn, PCI_COMMAND, cmd);
}

uint64_t pci_read_bar(uint8_t bus, uint8_t dev, uint8_t fn, int idx,
                      uint64_t *out_size) {
    uint16_t off = PCI_BAR0 + (uint16_t)(idx * 4);
    uint32_t lo  = pci_cfg_read32(bus, dev, fn, off);

    if (out_size) *out_size = 0;
    if (lo == 0) return 0;
    if (lo & 0x1) return 0;            /* I/O BAR — unsupported here */

    int is_64 = ((lo >> 1) & 0x3) == 0x2;
    uint32_t hi = is_64 ? pci_cfg_read32(bus, dev, fn, off + 4) : 0;
    uint64_t base = ((uint64_t)hi << 32) | (lo & ~0xFUL);

    if (out_size) {
        /* Sizing: per PCI spec, write 0xFFFFFFFF to both halves of a
         * 64-bit BAR before reading back, then restore. */
        pci_cfg_write32(bus, dev, fn, off, 0xFFFFFFFF);
        if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, 0xFFFFFFFF);
        uint32_t sz_lo = pci_cfg_read32(bus, dev, fn, off);
        uint32_t sz_hi = is_64 ? pci_cfg_read32(bus, dev, fn, off + 4) : 0;
        pci_cfg_write32(bus, dev, fn, off, lo);
        if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, hi);

        uint64_t mask = ((uint64_t)sz_hi << 32) | (sz_lo & ~0xFUL);
        *out_size = (~mask) + 1;
    }
    return base;
}

uint8_t pci_find_capability(uint8_t bus, uint8_t dev, uint8_t fn,
                            uint8_t cap_id, uint8_t start) {
    uint16_t status = pci_cfg_read16(bus, dev, fn, PCI_STATUS);
    if (!(status & PCI_STATUS_CAP_LIST)) return 0;

    uint8_t off = start
        ? pci_cfg_read8(bus, dev, fn, start + 1)
        : (pci_cfg_read8(bus, dev, fn, PCI_CAPABILITIES_PTR) & 0xFC);

    /* Bound walk to avoid loops on bogus hardware. */
    for (int i = 0; i < 48 && off; i++) {
        uint8_t id = pci_cfg_read8(bus, dev, fn, off);
        if (id == cap_id) return off;
        off = pci_cfg_read8(bus, dev, fn, off + 1) & 0xFC;
    }
    return 0;
}

int pci_find(uint16_t vendor, uint16_t device,
             uint8_t *bus_out, uint8_t *dev_out, uint8_t *fn_out) {
    /* QEMU virt: single host bridge, bus 0. Walk dev 0..31, fn 0..7. */
    for (uint8_t bus = 0; bus < 1; bus++) {
        for (uint8_t d = 0; d < 32; d++) {
            for (uint8_t f = 0; f < 8; f++) {
                uint16_t v = pci_cfg_read16(bus, d, f, PCI_VENDOR_ID);
                if (v == 0xFFFF) {
                    if (f == 0) break;   /* fn 0 absent → skip device */
                    continue;
                }
                uint16_t id = pci_cfg_read16(bus, d, f, PCI_DEVICE_ID);
                if (v == vendor && id == device) {
                    *bus_out = bus; *dev_out = d; *fn_out = f;
                    return 1;
                }
                /* Multi-function bit only checked at fn 0. */
                if (f == 0) {
                    uint8_t hdr = pci_cfg_read8(bus, d, f, PCI_HEADER_TYPE);
                    if (!(hdr & 0x80)) break;
                }
            }
        }
    }
    return 0;
}

/* Assign BARs of one device out of the linear MMIO allocator. Skips
 * unimplemented BARs and BARs already programmed by firmware. Base
 * addresses are kept inside the 32-bit window so they fit our high-half
 * map at PCI_BAR_WINDOW_BASE. */
static void pci_assign_bars(uint8_t bus, uint8_t dev, uint8_t fn,
                            uint64_t *next) {
    /* Disable MEM/IO decode while reprogramming BARs. */
    uint16_t cmd = pci_cfg_read16(bus, dev, fn, PCI_COMMAND);
    pci_cfg_write16(bus, dev, fn, PCI_COMMAND,
                    (uint16_t)(cmd & ~(PCI_CMD_MEMORY | PCI_CMD_IO)));

    for (int idx = 0; idx < 6; idx++) {
        uint16_t off = PCI_BAR0 + (uint16_t)(idx * 4);
        uint32_t lo  = pci_cfg_read32(bus, dev, fn, off);
        if (lo == 0xFFFFFFFF) continue;
        if (lo & 0x1)        continue;            /* I/O BAR */

        int is_64 = ((lo >> 1) & 0x3) == 0x2;

        /* Probe size: write all-1s to both halves, read back. */
        pci_cfg_write32(bus, dev, fn, off, 0xFFFFFFFF);
        if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, 0xFFFFFFFF);
        uint32_t sz_lo = pci_cfg_read32(bus, dev, fn, off);
        uint32_t sz_hi = is_64 ? pci_cfg_read32(bus, dev, fn, off + 4) : 0;
        uint64_t mask  = ((uint64_t)sz_hi << 32) | (sz_lo & ~0xFUL);
        if (mask == 0) {
            /* Unimplemented — restore original (zero) and move on. */
            pci_cfg_write32(bus, dev, fn, off, lo);
            if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, 0);
            if (is_64) idx++;
            continue;
        }
        uint64_t size = (~mask) + 1;

        /* Align next allocator pointer to size. */
        uint64_t addr = (*next + size - 1) & ~(size - 1);
        if (addr + size > PCI_BAR_WINDOW_BASE + PCI_BAR_WINDOW_SIZE) {
            /* Out of window — leave BAR unassigned. */
            pci_cfg_write32(bus, dev, fn, off, 0);
            if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, 0);
            if (is_64) idx++;
            continue;
        }
        *next = addr + size;

        /* Program BAR with new base, preserving the type bits. */
        pci_cfg_write32(bus, dev, fn, off, (uint32_t)addr | (lo & 0xF));
        if (is_64) pci_cfg_write32(bus, dev, fn, off + 4, (uint32_t)(addr >> 32));

        if (is_64) idx++;
    }

    /* Re-enable MEM decode + bus-master. */
    pci_cfg_write16(bus, dev, fn, PCI_COMMAND,
                    (uint16_t)(cmd | PCI_CMD_MEMORY | PCI_CMD_MASTER));
}

void pci_init(void) {
    uint64_t next = PCI_BAR_WINDOW_BASE;
    int n = 0;
    for (uint8_t d = 0; d < 32; d++) {
        for (uint8_t f = 0; f < 8; f++) {
            uint16_t v = pci_cfg_read16(0, d, f, PCI_VENDOR_ID);
            if (v == 0xFFFF) {
                if (f == 0) break;
                continue;
            }
            uint16_t id = pci_cfg_read16(0, d, f, PCI_DEVICE_ID);
            pci_assign_bars(0, d, f, &next);
            printk("pci: %x:%x.%x  vendor=%x device=%x\n", 0, d, f, v, id);
            n++;
            if (f == 0) {
                uint8_t hdr = pci_cfg_read8(0, d, f, PCI_HEADER_TYPE);
                if (!(hdr & 0x80)) break;
            }
        }
    }
    printk("pci: %d device(s), BAR window used 0x%lx..0x%lx\n",
           n, PCI_BAR_WINDOW_BASE, (unsigned long)next);
}
