#pragma once

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

extern pgtable_t kernel_pgtable;

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
    return SATP_SV39 | (((unsigned long)pgtable) >> 12);
}

void vmem_map(pgtable_t pgtable, unsigned long virt_addr, unsigned long phy_addr, unsigned long size, unsigned long permissions);
void vmem_init();
void vmem_init_post();