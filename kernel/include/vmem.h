#pragma once
#include <stdbool.h>

typedef unsigned long pte_t;
typedef unsigned long pde_t;
typedef unsigned long *pgtable_t;

// bit positions
#define PTE_V (1L << 0)
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4)

#define KVMEM_OFFSET 0xFFFFFFFF00000000UL

extern unsigned long mem_offset;

static inline int get_ptindx(int level, unsigned long virt_addr) {
    return (virt_addr >> (12 + 9*level)) & 0x1FF;
}

static inline unsigned long pte_to_phyaddr(unsigned long pte) {
    return ((pte) >> 10) << 12;
}

static inline unsigned long phyaddr_to_pte(unsigned long phy_addr) {
    return ((phy_addr) >> 12) << 10;
}

static inline void flush_tlb() {
    asm volatile("sfence.vma zero, zero");
}

#define SATP_SV39 (8L << 60)

static inline unsigned long make_satp(pgtable_t pgtable) {
    // pgtable is a kernel virtual address; convert to physical for SATP
    unsigned long pa = (unsigned long)pgtable - mem_offset;
    return SATP_SV39 | (pa >> 12);
}

// Convert kernel virtual address to physical address.
// Uses mem_offset so it works both before and after vmem_init().
static inline unsigned long virt_to_phys(unsigned long va) {
    return va - mem_offset;
}

// Convert physical address to kernel virtual address.
// Uses mem_offset so it works both before and after vmem_init().
static inline unsigned long phys_to_virt(unsigned long pa) {
    return pa + mem_offset;
}

// User virtual address space layout
#define USER_TEXT_BASE  0x1000UL          // first user code page
#define USER_STACK_TOP  0x40000000UL      // user stack grows down from here

extern pgtable_t kernel_pgtable;

// Kernel page table setup
void      vmem_init(void);

// Page table manipulation
pte_t    *get_pte(pgtable_t pgtable, unsigned long virt_addr, bool alloc);
void      vmem_map(pgtable_t pgtable, unsigned long virt_addr,
                   unsigned long phy_addr, unsigned long size,
                   unsigned long permissions);

// User address-space management
pgtable_t create_user_pgtable(void);
void      free_user_pgtable(pgtable_t pt);
int       map_stack(pgtable_t pt);  // maps one page at USER_STACK_TOP - PAGE_SIZE