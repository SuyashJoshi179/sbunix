#include <drivers/uart.h>
#include <pmem.h>
#include <printk.h>
#include <string.h>
#include <vmem.h>

extern char _text_end[];
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

    unsigned long tend_aligned = page_round_up((unsigned long)_text_end);

    // UART: identity + high half (for the transition period)
    vmem_map(pgtable, KVMEM_OFFSET + UART, UART, PAGE_SIZE, PTE_R | PTE_W);
    vmem_map(pgtable, UART,               UART, PAGE_SIZE, PTE_R | PTE_W);

    // Kernel text: identity + high half
    vmem_map(pgtable, KVMEM_OFFSET + KERN_BASE, KERN_BASE,
             tend_aligned - KERN_BASE, PTE_R | PTE_X);
    vmem_map(pgtable, KERN_BASE, KERN_BASE,
             tend_aligned - KERN_BASE, PTE_R | PTE_X);

    // Heap (data + free pages): high half only is enough, but also identity
    // so that the freelist nodes are reachable until we clear identity maps.
    vmem_map(pgtable, KVMEM_OFFSET + tend_aligned, tend_aligned,
             PHYMEM_END - tend_aligned, PTE_R | PTE_W);
    vmem_map(pgtable, tend_aligned, tend_aligned,
             PHYMEM_END - tend_aligned, PTE_R | PTE_W);

    return pgtable;
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

    // Remove temporary identity mappings (not needed now we're in high half).
    kernel_pgtable[get_ptindx(2, KERN_BASE)] = 0;
    kernel_pgtable[get_ptindx(2, UART)]      = 0;
    flush_tlb();

    printk("We are in virtual memory!\n");
}
