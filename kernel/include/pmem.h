#pragma once

#define KERN_BASE 0x80000000
#define PAGE_SIZE 4096UL
#define PHYMEM_END 0x88000000UL

static inline unsigned long page_round_up(unsigned long addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void  pmem_init(void *start, void *end);
void  pmem_rebase(unsigned long offset);
void *page_alloc(void);
void  page_free(void *page);
