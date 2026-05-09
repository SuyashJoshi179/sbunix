#include <page_ref.h>
#include <pmem.h>
#include <printk.h>
#include <vmem.h>

/* uint16 supports up to 65535 references per page. uint8 (255) overflowed
 * once page caps were removed and oom_test fork()ed 256 children sharing
 * an 8192-page heap CoW. */
static unsigned short page_refs[NPAGES];
int page_refs_ready = 0;

static unsigned long pa2pfn(unsigned long pa) {
    return (pa - KERN_BASE) / PAGE_SIZE;
}

void page_ref_init(void) {
    for (unsigned long i = 0; i < NPAGES; i++)
        page_refs[i] = 0;
    page_refs_ready = 1;
}

void page_ref_set(unsigned long pa, unsigned short val) {
    page_refs[pa2pfn(pa)] = val;
}

unsigned short page_ref_get(unsigned long pa) {
    return page_refs[pa2pfn(pa)];
}

void page_get(unsigned long pa) {
    unsigned long pfn = pa2pfn(pa);
    if (page_refs[pfn] == 65535)
        panic("page_get: refcount overflow");
    page_refs[pfn]++;
}

void page_put(unsigned long pa) {
    unsigned long pfn = pa2pfn(pa);
    if (page_refs[pfn] == 0)
        panic("page_put: refcount underflow");
    page_refs[pfn]--;
    if (page_refs[pfn] == 0)
        page_free((void *)phys_to_virt(pa));
}
