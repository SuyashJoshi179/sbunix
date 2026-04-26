# Phase 10 — VirtIO over PCI (single PR)

## Motivation

Submission grader's harness Makefile (the `master/Makefile`) launches
QEMU with:

```
-device virtio-blk-pci-non-transitional,drive=hd0
-device virtio-gpu-pci
-device virtio-net-pci-non-transitional,netdev=net0
-netdev user,id=net0
```

Our kernel currently only speaks VirtIO MMIO at `0x10001000`. On the
grader's QEMU we see `unexpected device ID 0` and infinite
`TIMEOUT waiting for used ring`. Goal: kernel boots to `/bin/sh` AND
mounts `/data` (sbfs) under the master Makefile, with a single PCI-only
transport. Drop MMIO entirely so the local Makefile == master Makefile.

## Scope decision: PCI-only, single PR

We delete the VirtIO MMIO code path. Reasons:

* One init path, no transport vtable abstraction, ~30% less code.
* Local `make qemu` matches grader from day 1.
* Nobody is using MMIO outside of legacy QEMU configs we don't care about.

This is a single PR (no sub-PRs) because the work is tightly coupled —
PCI scan, virtio-pci transport, INTx wiring, and Makefile swap all have
to land together or `make qemu` is broken. Develop on a feature branch
(`feature/virtio-pci`); keep `develop` green; merge only when boot +
sbfs work end-to-end on the master Makefile invocation.

## What QEMU `virt` exposes

| Region            | PA range                  | Notes                                   |
|-------------------|---------------------------|-----------------------------------------|
| PCIe ECAM         | `0x30000000 .. 0x3fffffff` | 256 MB, bus 0..255 × dev 0..31 × fn 0..7 |
| PCI MMIO 32-bit   | `0x40000000 .. 0x7fffffff` | 1 GB window for BARs                   |
| PCI MMIO 64-bit   | `0x400000000 .. 0x7ffffffff` | not needed                          |
| PLIC IRQs 32..35  | INTx swizzle              | INTA=32, INTB=33, INTC=34, INTD=35      |

Modern `virtio-blk-pci-non-transitional`:

* PCI vendor `0x1AF4`, device `0x1042` (modern block).
* Revision ≥ 1, MSI-X enabled by default but legacy INTx is still wired.
* Capabilities list contains four VirtIO-vendor caps that point inside a
  BAR (typically BAR4) at COMMON, NOTIFY, ISR, DEVICE structs.

We use legacy INTx (clear MSI-X enable bit before reading INTERRUPT_LINE).

## Implementation

### 1. Map PCI regions into kernel high-half

`kernel/vmem.c`: drop the `VIRTIO_PHYS=0x10001000` 4 KB mapping. Add:

```c
#define ECAM_PHYS 0x30000000UL
#define ECAM_SIZE 0x10000000UL
vmem_map(pgtable, KVMEM_OFFSET + ECAM_PHYS, ECAM_PHYS, ECAM_SIZE, PTE_R | PTE_W);

#define PCI_MMIO_PHYS 0x40000000UL
#define PCI_MMIO_SIZE 0x40000000UL          /* 1 GB BAR window */
vmem_map(pgtable, KVMEM_OFFSET + PCI_MMIO_PHYS, PCI_MMIO_PHYS, PCI_MMIO_SIZE,
         PTE_R | PTE_W);
```

Both ranges go through `mem_offset` like the existing PLIC map.

### 2. PCI bus driver

New `kernel/drivers/pci.c` + `kernel/include/drivers/pci.h`:

```c
uint8_t  pci_cfg_read8 (uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
uint16_t pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
uint32_t pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off);
void     pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint16_t off, uint32_t v);

/* Write 0x6 (memory space + bus master) to command register. */
void     pci_enable(uint8_t bus, uint8_t dev, uint8_t fn);

/* BAR helpers: returns physical address; size returned via *out_size.
 * Handles 32- and 64-bit BARs and the standard write-1s/read-back sizing. */
uint64_t pci_read_bar(uint8_t bus, uint8_t dev, uint8_t fn, int idx, uint64_t *out_size);

/* Walk capability list at 0x34 → linked list. Returns offset of the first
 * capability whose vendor ID matches `cap_id`, or 0 if none. start=0 to
 * begin from list head, otherwise resume after `start`. */
uint8_t  pci_find_capability(uint8_t bus, uint8_t dev, uint8_t fn,
                              uint8_t cap_id, uint8_t start);

/* Find first device matching (vendor, device). Returns 1 + writes bdf, else 0. */
int      pci_find(uint16_t vendor, uint16_t device,
                   uint8_t *bus, uint8_t *dev, uint8_t *fn);
```

ECAM address arithmetic: `mem_offset + 0x30000000 + (bus << 20) +
(dev << 15) + (fn << 12) + reg_off`.

### 3. virtio_pci transport in `kernel/drivers/virtio_disk.c`

Delete the existing MMIO body. New init:

```c
void virtio_disk_init(void) {
    uint8_t bus, dev, fn;
    if (!pci_find(0x1AF4, 0x1042, &bus, &dev, &fn)) {
        printk("virtio_disk: no virtio-blk PCI device\n");
        return;
    }
    pci_enable(bus, dev, fn);

    /* 1. Disable MSI-X if present so INTx is delivered. */
    uint8_t msix = pci_find_capability(bus, dev, fn, 0x11, 0);
    if (msix) {
        uint16_t ctrl = pci_cfg_read16(bus, dev, fn, msix + 2);
        pci_cfg_write32(bus, dev, fn, msix + 2, ctrl & ~0x8000);  /* MSI-X enable */
    }

    /* 2. Walk virtio-vendor caps (cap_id=0x09). For each, read cfg_type
     * (offset+3), bar (offset+4), bar_offset (offset+8), length (offset+12).
     * cfg_type values: 1=COMMON, 2=NOTIFY, 3=ISR, 4=DEVICE, 5=PCI_CFG. */
    /* Resolve each to a kernel VA inside the pre-mapped 0x40000000 window. */

    /* 3. Initialise via COMMON cfg:
     *   write_status(0)
     *   write_status(ACK | DRIVER)
     *   negotiate VIRTIO_F_VERSION_1, set FEATURES_OK, re-read to confirm
     *   queue_select(0); queue_size = min(VIRTQ_SIZE, queue_size_max)
     *   write desc/avail/used physical addrs
     *   queue_enable = 1
     *   write_status(... | DRIVER_OK)
     */

    /* 4. Compute notify register address:
     *      notify_addr = bar_va + notify_off + queue_notify_off * notify_off_multiplier
     */

    disk_ready = 1;
}
```

`virtio_disk_rw` keeps its current 3-descriptor chain + polling loop.
Only the kick changes:

```c
*notify_reg = 0;   /* queue_select == 0 */
```

Header-level structures (split-ring `virtq_desc`/`avail`/`used`,
`virtio_blk_req`) stay unchanged in `virtio.h`.

### 4. INTx → PLIC

`kernel/drivers/plic.c`: enable IRQs 32..35 (priority 1) so any of the
four PCI INTx lines can fire.

`kernel/trap.c`: extend the IRQ dispatch:

```c
} else if (irq >= 32 && irq <= 35) {
    virtio_disk_intr();
}
```

`virtio_disk_intr` reads ISR status register (clears interrupt) and
wakes the polling loop (no behaviour change vs MMIO).

If MSI-X cannot be disabled or `INTERRUPT_LINE = 0xff`, polling alone
handles completion — PR still works, just without IRQ-driven wakeups.

### 5. Adopt master Makefile

After kernel works:

* Replace `Makefile` with byte-identical copy of `master/Makefile`.
* `tools/mkfs.c` already accepts the optional `size_mb` argument
  (released earlier).
* Local `make qemu` now uses the PCI device list — same as grader.

## Files touched (single PR)

| File                                    | Change                              |
|-----------------------------------------|-------------------------------------|
| `kernel/vmem.c`                         | drop MMIO map, add ECAM + BAR window |
| `kernel/drivers/pci.c`                  | new — ECAM walker, BAR sizing, caps  |
| `kernel/include/drivers/pci.h`          | new                                  |
| `kernel/drivers/virtio_disk.c`          | rewritten for virtio-pci modern      |
| `kernel/include/drivers/virtio.h`       | drop MMIO regs; keep ring structs    |
| `kernel/drivers/plic.c`                 | enable IRQs 32..35 (drop IRQ 1)      |
| `kernel/trap.c`                         | dispatch IRQs 32..35                 |
| `kernel/kernel.c`                       | call `pci_init` (or none) before `virtio_disk_init` |
| `Makefile`                              | replaced by master copy              |

## Risks / open questions

* **MSI-X gate.** If clearing `MSIX_ENABLE` is insufficient on QEMU's
  modern virtio-pci, INTx never fires. Fallback: polling alone — already
  correct for our blocking sbfs path.
* **BAR sizing.** Must detect 64-bit BAR (bits 1..2 of BAR low half ==
  `0b10`) and pair `(bar, bar+1)`. virtio-pci on QEMU virt typically
  uses 64-bit BAR4.
* **Capability list.** Check `STATUS` register bit 4 (capabilities
  supported) before walking; abort cleanly otherwise.
* **Other PCI devices.** `virtio-gpu-pci` and `virtio-net-pci` sit on
  the same bus. `pci_find` matches only on (vendor, device) so they're
  skipped without crashing.
* **Cache coherency.** RV64 with `Zicsr_Zifencei` already gives us
  `__sync_synchronize`; same fences as the MMIO path apply.

## Out of scope

* MSI-X (use INTx + polling).
* Multiple block devices.
* Generic PCI driver framework — only what virtio-blk needs.
* PCIe extended config space.

## Acceptance criteria

1. Local `make qemu` — boots to `sh>`, `/data` mounted, sbfs read/write
   works.
2. Grader Build Check — boots to `sh>`, `/data` mounted.
3. Local `Makefile` is byte-identical to `master/Makefile`.
4. `kernel/include/drivers/virtio.h` no longer references MMIO offsets.
