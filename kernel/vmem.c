#include <drivers/uart.h>
#include <page_ref.h>
#include <pmem.h>   // pmem_rebase
#include <printk.h>
#include <string.h>
#include <vmem.h>

extern char _text_end[];
extern char _kernel_end[];
extern void vmem_switch_to_high(unsigned long satp, unsigned long offset);

unsigned long mem_offset = 0;

pgtable_t kernel_pgtable;

// Walk the 3-level SV39 page table to find (or allocate) the leaf PTE for
// virt_addr.  All page table pointers are kernel virtual addresses after
// vmem_init(); during early boot mem_offset=0 so virtual == physical.
pte_t *get_pte(pgtable_t pgtable, unsigned long virt_addr, bool alloc) {
    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pgtable[get_ptindx(level, virt_addr)];
        if (*pte & PTE_V) {
            // PTE stores a physical address; convert to kernel virtual
            pgtable = (pgtable_t)phys_to_virt(pte_to_phyaddr(*pte));
        } else {
            if (!alloc)
                return 0;
            // page_alloc() now returns a kernel virtual address
            pgtable = (pgtable_t)page_alloc();
            if (pgtable == 0) return 0;
            // Store physical address in PTE
            *pte = phyaddr_to_pte(virt_to_phys((unsigned long)pgtable)) | PTE_V;
        }
    }
    return &pgtable[get_ptindx(0, virt_addr)];
}

// Map size bytes of physical memory starting at phy_addr into the page table
// at virtual address virt_addr with the given permission bits.
void vmem_map(pgtable_t pgtable, unsigned long virt_addr, unsigned long phy_addr,
              unsigned long size, unsigned long permissions) {
    unsigned long addr_end = phy_addr + size;
    for (unsigned long addr = phy_addr; addr < addr_end; addr += PAGE_SIZE) {
        pte_t *pte = get_pte(pgtable, virt_addr, true);
        if (pte == 0) return;
        *pte = phyaddr_to_pte(addr) | permissions | PTE_V;
        virt_addr += PAGE_SIZE;
    }
}

// Allocate and populate the kernel page table.
// Called before vmem_switch_to_high(), so mem_offset=0 and page_alloc()
// returns physical addresses that are accessible via identity mapping.
static pgtable_t vmem_create(void) {
    pgtable_t pgtable = (pgtable_t)page_alloc();
    // page_alloc already zeroes the page

    // tend_aligned: page boundary after .text end.
    // The last text page also holds the start of .data (RISC-V linker packs
    // sections tightly), so it must be both executable AND writable.
    unsigned long tend_aligned = page_round_up((unsigned long)_text_end);
    // kend_aligned: page boundary after the full kernel image (.text+.data+.bss+stack)
    unsigned long kend_aligned = page_round_up((unsigned long)_kernel_end);

    // UART: identity + high half (for the transition period)
    vmem_map(pgtable, KVMEM_OFFSET + UART, UART, PAGE_SIZE, PTE_R | PTE_W);
    vmem_map(pgtable, UART,               UART, PAGE_SIZE, PTE_R | PTE_W);

    // PLIC: high-half only (accessed after vmem_init enables VM).
    // Maps 0x0c000000–0x0c3fffff (4 MB) covering all PLIC register regions.
    #define PLIC_PHYS 0x0c000000UL
    #define PLIC_MAPSZ 0x400000UL
    vmem_map(pgtable, KVMEM_OFFSET + PLIC_PHYS, PLIC_PHYS, PLIC_MAPSZ, PTE_R | PTE_W);

    // VirtIO MMIO: first slot at 0x10001000 (4 KB).
    // QEMU virt board maps virtio-mmio-bus.0 here; IRQ 1 on the PLIC.
    #define VIRTIO_PHYS 0x10001000UL
    #define VIRTIO_SIZE 0x1000UL
    vmem_map(pgtable, KVMEM_OFFSET + VIRTIO_PHYS, VIRTIO_PHYS, VIRTIO_SIZE, PTE_R | PTE_W);

    // Kernel text + mixed page: identity + high half, R|W|X
    // Using R|W|X because the last code page also contains .data variables
    // that must be writable.  Code-only pages could be R|X, but the gain
    // is not worth the complexity in an educational kernel.
    vmem_map(pgtable, KVMEM_OFFSET + KERN_BASE, KERN_BASE,
             tend_aligned - KERN_BASE, PTE_R | PTE_W | PTE_X);
    vmem_map(pgtable, KERN_BASE, KERN_BASE,
             tend_aligned - KERN_BASE, PTE_R | PTE_W | PTE_X);

    // Kernel data/bss/stack (after the mixed page): identity + high half, R|W
    if (kend_aligned > tend_aligned) {
        vmem_map(pgtable, KVMEM_OFFSET + tend_aligned, tend_aligned,
                 kend_aligned - tend_aligned, PTE_R | PTE_W);
        vmem_map(pgtable, tend_aligned, tend_aligned,
                 kend_aligned - tend_aligned, PTE_R | PTE_W);
    }

    // Free heap pages: identity + high half, R|W
    vmem_map(pgtable, KVMEM_OFFSET + kend_aligned, kend_aligned,
             PHYMEM_END - kend_aligned, PTE_R | PTE_W);
    vmem_map(pgtable, kend_aligned, kend_aligned,
             PHYMEM_END - kend_aligned, PTE_R | PTE_W);

    return pgtable;
}

// ---------------------------------------------------------------------------
// User address-space helpers
// ---------------------------------------------------------------------------

// Allocate a new page table for a user process.
// Copies the kernel upper-half L2 entries (indices 256-511) from
// kernel_pgtable so that the kernel is accessible from user processes.
// The lower half (user virtual addresses) starts empty.
pgtable_t create_user_pgtable(void) {
    pgtable_t pt = (pgtable_t)page_alloc();
    if (!pt) return 0;
    // Copy ALL 512 L2 entries from kernel_pgtable.
    //
    // After vmem_init() the kernel continues running at PHYSICAL addresses
    // (boot() returns to its physical RA after vmem_switch_to_high adjusts
    // only vmem_init's own frame).  This means stvec, function pointers,
    // and all kernel symbols are physical (0x80200xxx, VPN[2]=2).
    //
    // If we only copied the upper half (256–511) then, the moment a trap
    // fires with user SATP active, the CPU would fault on the stvec fetch.
    //
    // Solution: copy the full kernel page table so kernel identity mappings
    // (e.g. VPN[2]=2 for KERN_BASE) remain accessible when S-mode code runs
    // under the user SATP.  Kernel pages do NOT have PTE_U set, so U-mode
    // code cannot reach them; S-mode can (PTE_U restriction only applies to
    // U-mode, or to S-mode if sstatus.SUM=0).
    for (int i = 0; i < 512; i++)
        pt[i] = kernel_pgtable[i];
    // User-space mappings (code, stack) are added later via vmem_map()
    // which will overwrite the VPN[2]=0 slot (currently 0 from kernel_pgtable).
    return pt;
}

// Recursively free all physical pages mapped in the user half of a page table
// (L2 indices 0–255, i.e., virtual addresses below KVMEM_OFFSET).
// Also frees the intermediate page table pages.
static void free_user_pages_level(pgtable_t pt, int level) {
    int limit = (level == 2) ? 256 : 512;
    for (int i = 0; i < limit; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V)) continue;
        // At L2, skip entries shared with the kernel page table; those page
        // table nodes are not owned by this process and must not be freed.
        if (level == 2 && pte == kernel_pgtable[i]) continue;
        unsigned long pa = pte_to_phyaddr(pte);
        if (pte & (PTE_R | PTE_W | PTE_X)) {
            page_put(pa);
        } else if (level > 0) {
            // Pointer PTE — recurse into child page table, then free it
            pgtable_t child = (pgtable_t)phys_to_virt(pa);
            free_user_pages_level(child, level - 1);
            page_free((void *)child);
        }
    }
}

void free_user_pgtable(pgtable_t pt) {
    if (!pt) return;
    free_user_pages_level(pt, 2);
    page_free(pt);
}

// Map one freshly-allocated page as the user stack just below USER_STACK_TOP.
// Returns the kernel virtual address of the page, or NULL on OOM.
void *map_stack(pgtable_t pt) {
    void *page = page_alloc();
    if (!page) return 0;
    vmem_map(pt, USER_STACK_TOP - PAGE_SIZE,
             virt_to_phys((unsigned long)page),
             PAGE_SIZE, PTE_R | PTE_W | PTE_U);
    return page;
}

pgtable_t uvmcow_share(pgtable_t parent_pt) {
    pgtable_t child_pt = create_user_pgtable();
    if (!child_pt) return 0;

    for (int l2i = 0; l2i < 256; l2i++) {
        pte_t l2pte = parent_pt[l2i];
        if (!(l2pte & PTE_V)) continue;
        if (l2pte == kernel_pgtable[l2i]) continue;
        pgtable_t l1 = (pgtable_t)phys_to_virt(pte_to_phyaddr(l2pte));

        for (int l1i = 0; l1i < 512; l1i++) {
            pte_t l1pte = l1[l1i];
            if (!(l1pte & PTE_V)) continue;
            pgtable_t l0 = (pgtable_t)phys_to_virt(pte_to_phyaddr(l1pte));

            for (int l0i = 0; l0i < 512; l0i++) {
                pte_t pte = l0[l0i];
                if (!(pte & PTE_V)) continue;
                if (!(pte & (PTE_R | PTE_W | PTE_X))) continue;

                unsigned long pa = pte_to_phyaddr(pte);
                page_get(pa);

                unsigned long perm = pte & (PTE_R | PTE_W | PTE_X | PTE_U);
                if (perm & PTE_W) {
                    perm &= ~PTE_W;
                    l0[l0i] = phyaddr_to_pte(pa) | perm | PTE_V;
                }

                unsigned long vaddr = ((unsigned long)l2i << 30) |
                                      ((unsigned long)l1i << 21) |
                                      ((unsigned long)l0i << 12);

                pte_t *child_pte = get_pte(child_pt, vaddr, 1);
                if (!child_pte) {
                    free_user_pgtable(child_pt);
                    flush_tlb();
                    return 0;
                }
                *child_pte = phyaddr_to_pte(pa) | perm | PTE_V;
            }
        }
    }
    flush_tlb();
    return child_pt;
}

void uvmunmap_range(pgtable_t pt, unsigned long va_start, unsigned long va_end) {
    for (unsigned long va = va_start; va < va_end; va += PAGE_SIZE) {
        pte_t *pte = get_pte(pt, va, 0);
        if (!pte || !(*pte & PTE_V)) continue;
        if (!(*pte & (PTE_R | PTE_W | PTE_X))) continue;
        unsigned long pa = pte_to_phyaddr(*pte);
        page_put(pa);
        *pte = 0;
    }
    flush_tlb();
}

void vmem_init(void) {
    // kernel_pgtable is physical here (mem_offset still 0)
    kernel_pgtable = vmem_create();

    printk("Kernel page table created, enabling virtual memory..\n");
    vmem_switch_to_high(make_satp(kernel_pgtable), KVMEM_OFFSET);

    // From here sp and ra are in high-half virtual space.
    mem_offset = KVMEM_OFFSET;

    // Rebase kernel_pgtable pointer to its kernel virtual address.
    kernel_pgtable = (pgtable_t)phys_to_virt((unsigned long)kernel_pgtable);

    // Rebase the physical memory freelist to kernel virtual addresses.
    // Must happen BEFORE clearing identity mappings so both phys and
    // virt addresses are simultaneously accessible during the walk.
    pmem_rebase(KVMEM_OFFSET);

    // Remove the UART identity mapping (low address 0x10000000).
    // We keep the kernel text/heap identity mapping (0x80000000 range) so that
    // the CPU can continue executing boot() at its physical return address after
    // this function returns.  User page tables only copy upper-half L2 entries
    // (256-511), so user mode can never reach the physical-address mappings.
    kernel_pgtable[get_ptindx(2, UART)] = 0;
    flush_tlb();

    printk("We are in virtual memory!\n");
}
