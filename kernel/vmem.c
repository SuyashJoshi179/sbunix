#include<drivers/uart.h>
#include<pmem.h>
#include<printk.h>
#include<string.h>
#include<vmem.h>

// virtio disk interface is memory mapped at this address
#define VIRTIO0 0x10001000

extern char _text_end[];
extern void vmem_switch_to_high(unsigned long satp, unsigned long offset);

unsigned long mem_offset = 0;

pgtable_t kernel_pgtable;

// get the addr of pte in page table, which currosponds to given virt_addr
// if alloc is true, we allocate
pte_t* get_pte(pgtable_t pgtable, unsigned long virt_addr, bool alloc) {
    for(int level = 2; level > 0; level--) {
        pte_t *pte = &pgtable[get_ptindx(level, virt_addr)];
        if(*pte & PTE_V) {
            pgtable = (pgtable_t)phys_to_virt(pte_to_phyaddr(*pte));
        } else {
            if(!alloc) {
                return 0;
            }
            pgtable = (pde_t*) page_alloc();
            if(pgtable == 0) return 0;
            memset(pgtable, 0, PAGE_SIZE);
            *pte = phyaddr_to_pte(virt_to_phys((unsigned long)pgtable)) | PTE_V;
        }
    }
    return &pgtable[get_ptindx(0, virt_addr)];
}

// creare mapping in page table
void vmem_map(pgtable_t pgtable, unsigned long virt_addr, unsigned long phy_addr, unsigned long size, unsigned long permissions) {
    // todo - perform validations
    
    unsigned long addr_end = phy_addr + size;
    pte_t *pte;
    for(unsigned long addr = phy_addr; addr < addr_end; addr += PAGE_SIZE) {
        pte = get_pte(pgtable, virt_addr, true);
        if(pte == 0) return;
        *pte = phyaddr_to_pte(addr) | permissions | PTE_V;
        virt_addr += PAGE_SIZE;
    }
}

pgtable_t vmem_create() {
    pgtable_t pgtable;
    pgtable = (pgtable_t) page_alloc();

    // format all to zeros initially
    memset(pgtable, 0, PAGE_SIZE);

    // map the memory mapped io at higher memory address
    vmem_map(pgtable, KVMEM_OFFSET+UART, UART, PAGE_SIZE, PTE_R | PTE_W);
    vmem_map(pgtable, UART, UART, PAGE_SIZE, PTE_R | PTE_W);

    // map virtio disk interface
    vmem_map(pgtable, KVMEM_OFFSET + VIRTIO0, VIRTIO0, PAGE_SIZE, PTE_R | PTE_W);
    vmem_map(pgtable, VIRTIO0, VIRTIO0, PAGE_SIZE, PTE_R | PTE_W);

    unsigned long tend_aligned = page_round_up((unsigned long)_text_end);

    // map the kernel text section at both identity and higher memory address, so as to make transition
    vmem_map(pgtable, KVMEM_OFFSET+KERN_BASE, KERN_BASE, tend_aligned - KERN_BASE, PTE_R | PTE_X);
    vmem_map(pgtable, KERN_BASE, KERN_BASE, tend_aligned - KERN_BASE, PTE_R | PTE_X);

    // map the physical meory to higher memory address
    vmem_map(pgtable, KVMEM_OFFSET+tend_aligned, tend_aligned, PHYMEM_END-tend_aligned, PTE_R | PTE_W);

    // todo - map process stacks

    return pgtable;

}

void vmem_init() {
    kernel_pgtable = vmem_create();

    printk("Kernel page table created, enabling virtual memory..\n");
}

void vmem_init_post() {
    mem_offset = KVMEM_OFFSET;

    // rebase root page table pointer into higher-half direct map.
    kernel_pgtable = (pgtable_t)phys_to_virt((unsigned long)kernel_pgtable);

    // clear temporary identity mappings after jumping to higher half.
    //kernel_pgtable[get_ptindx(2, KERN_BASE)] = 0;
    //kernel_pgtable[get_ptindx(2, UART)] = 0;
    flush_tlb();

    printk("We are in virtual memory!\n");
}