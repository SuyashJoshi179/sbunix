#pragma once
#include <pmem.h>

#define NPAGES ((PHYMEM_END - KERN_BASE) / PAGE_SIZE)

void    page_ref_init(void);
void    page_ref_set(unsigned long pa, unsigned char val);
unsigned char page_ref_get(unsigned long pa);
void    page_get(unsigned long pa);
void    page_put(unsigned long pa);
