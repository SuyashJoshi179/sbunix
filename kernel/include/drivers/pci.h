#pragma once
#include <stdint.h>

/* -----------------------------------------------------------------------
 * PCIe ECAM driver — QEMU virt board
 *
 * QEMU exposes 256 MB of PCIe Enhanced Configuration Access Mechanism
 * space at physical 0x30000000. Each (bus, dev, fn) maps to a 4 KB
 * config region: offset = (bus << 20) | (dev << 15) | (fn << 12).
 *
 * INTx lines route to PLIC IRQs 32..35 (INTA..INTD).
 * BARs allocated from the 32-bit MMIO window 0x40000000..0x7fffffff.
 * ----------------------------------------------------------------------- */

/* Standard config-space register offsets */
#define PCI_VENDOR_ID        0x00
#define PCI_DEVICE_ID        0x02
#define PCI_COMMAND          0x04
#define PCI_STATUS           0x06
#define PCI_HEADER_TYPE      0x0E
#define PCI_BAR0             0x10
#define PCI_CAPABILITIES_PTR 0x34
#define PCI_INTERRUPT_LINE   0x3C
#define PCI_INTERRUPT_PIN    0x3D

/* Command register bits */
#define PCI_CMD_IO           0x0001
#define PCI_CMD_MEMORY       0x0002
#define PCI_CMD_MASTER       0x0004

/* Status register bit 4 = capabilities list present */
#define PCI_STATUS_CAP_LIST  0x0010

/* Capability IDs */
#define PCI_CAP_ID_MSIX      0x11
#define PCI_CAP_ID_VENDOR    0x09   /* virtio uses vendor caps */

/* Scan PCIe ECAM, log discovered devices, and assign BAR addresses
 * out of the 32-bit MMIO window at 0x40000000. Call after vmem_init. */
void pci_init(void);

/* PCI MMIO window where BARs are placed. */
#define PCI_BAR_WINDOW_BASE 0x40000000UL
#define PCI_BAR_WINDOW_SIZE 0x40000000UL

/* Config-space accessors. off must be aligned for the read width. */
uint8_t  pci_cfg_read8 (uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
void     pci_cfg_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint16_t v);
void     pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v);

/* Set memory-space + bus-master bits in COMMAND. */
void pci_enable(uint8_t bus, uint8_t dev, uint8_t fn);

/* Read BAR `idx` (0..5). Decodes 32- and 64-bit memory BARs and sizes
 * via the standard write-1s/read-back protocol. Returns physical base;
 * size returned via *out_size (0 if BAR unimplemented). */
uint64_t pci_read_bar(uint8_t bus, uint8_t dev, uint8_t fn, int idx,
                      uint64_t *out_size);

/* Walk the capability list. Returns config offset of first capability
 * matching cap_id, or 0 if none. Pass start=0 for first lookup; pass
 * a previously returned offset to resume the walk. */
uint8_t pci_find_capability(uint8_t bus, uint8_t dev, uint8_t fn,
                            uint8_t cap_id, uint8_t start);

/* Find first device matching (vendor, device). Returns 1 + writes
 * bdf out-params on hit, 0 on miss. */
int pci_find(uint16_t vendor, uint16_t device,
             uint8_t *bus, uint8_t *dev, uint8_t *fn);
