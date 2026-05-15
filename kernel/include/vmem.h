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
#define PTE_A (1L << 6)
#define PTE_D (1L << 7)

/* Leaf PTE bits set unconditionally for any RWX mapping. Avoids soft
 * A/D-update faults on Svade hardware (no Svadu). xv6-riscv style. */
#define PTE_LEAF_AD (PTE_A | PTE_D)

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

/* Per-VA TLB shootdown (single-hart). Cheaper than the full flush when
 * we're only invalidating a known page (e.g. PTE_W flip on CoW fault,
 * file-backed unmap). RISC-V SFENCE.VMA rs1, x0 invalidates entries
 * for the virtual address in rs1 across all ASIDs. */
static inline void flush_tlb_page(unsigned long va) {
    asm volatile("sfence.vma %0, zero" :: "r" (va) : "memory");
}

#define SATP_SV39 (8L << 60)

static inline unsigned long make_satp(pgtable_t pgtable) {
    // pgtable is a kernel virtual address; convert to physical for SATP
    unsigned long pa = (unsigned long)pgtable - mem_offset;
    return SATP_SV39 | (pa >> 12);
}

// Convert kernel virtual address to physical address.
//
// Two cases after vmem_init():
//   (a) High-virtual addresses (>= KVMEM_OFFSET) — from page_alloc() after
//       pmem_rebase.  PA = VA - KVMEM_OFFSET.
//   (b) Physical/identity-mapped addresses (< KVMEM_OFFSET) — kernel BSS,
//       .data, and .text, which are still referenced by their link-time
//       physical addresses (0x80XXXXXX) via the identity mapping.  PA = VA.
//
// Before vmem_init(), mem_offset==0 and all addresses are physical; the
// < KVMEM_OFFSET branch handles that correctly too.
static inline unsigned long virt_to_phys(unsigned long va) {
    if (va < (unsigned long)KVMEM_OFFSET)
        return va;            /* identity-mapped: VA already is PA */
    return va - (unsigned long)KVMEM_OFFSET;
}

// Convert physical address to kernel virtual address.
// Uses mem_offset so it works both before and after vmem_init().
static inline unsigned long phys_to_virt(unsigned long pa) {
    return pa + mem_offset;
}

// User virtual address space layout (Sv39: user VA up to 256 GB).
// Regions are segregated: HEAP_ARENA is reserved for libc malloc's mmap'd
// arena (placed via MAP_FIXED). MMAP window is for explicit user mmap()
// calls (top-down search). Stack lives at the very top, grow-down,
// gated at runtime by RLIMIT_STACK.
#define USER_TEXT_BASE     0x0000000000001000UL  /* first user code page         */
#define HEAP_ARENA_BASE    0x0000000100000000UL  /* 4 GB                         */
#define HEAP_ARENA_END     0x0000001000000000UL  /* 64 GB                        */
#define MMAP_BASE          HEAP_ARENA_END        /* 64 GB                        */
#define MMAP_END_VA        0x0000002000000000UL  /* 128 GB                       */
#define USER_STACK_TOP     0x0000004000000000UL  /* 256 GB                       */

#define DEFAULT_STACK_SOFT (8UL  * 1024 * 1024)  /* 8 MB                         */
#define DEFAULT_STACK_HARD (64UL * 1024 * 1024)  /* 64 MB                        */
#define DEFAULT_STACK_MAX  DEFAULT_STACK_HARD

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
void     *map_stack(pgtable_t pt);     // maps one page at USER_STACK_TOP - PAGE_SIZE; returns kpage
pgtable_t uvmcow_share(pgtable_t parent);  // COW share user address space for fork()
void      uvmunmap_range(pgtable_t pt, unsigned long va_start, unsigned long va_end);

// Safe user access helpers for current process user pointers.
// Return 0 on success, -EFAULT on invalid/unmapped user memory.
int       copyin(void *kdst, const void *usrc, unsigned long n);
int       copyout(void *udst, const void *ksrc, unsigned long n);