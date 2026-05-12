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

/* Validate that `pa` names a page-aligned, in-range RAM page before
 * indexing page_refs[]. A garbage PA from a corrupted PTE would
 * otherwise either silently corrupt an out-of-array slot or trip the
 * underflow panic below with a confusing diagnostic. page_free does
 * the same check; keep them symmetric. */
static void check_page_pa(unsigned long pa, const char *who) {
    if ((pa & (PAGE_SIZE - 1)) != 0)
        panic(who);
    if (pa < KERN_BASE || pa >= PHYMEM_END)
        panic(who);
}

void page_get(unsigned long pa) {
    check_page_pa(pa, "page_get: pa out of range");
    unsigned long pfn = pa2pfn(pa);
    if (page_refs[pfn] == 65535)
        panic("page_get: refcount overflow");
    page_refs[pfn]++;
}

void page_put(unsigned long pa) {
    check_page_pa(pa, "page_put: pa out of range");
    unsigned long pfn = pa2pfn(pa);
    if (page_refs[pfn] == 0)
        panic("page_put: refcount underflow");
    page_refs[pfn]--;
    if (page_refs[pfn] == 0)
        page_free((void *)phys_to_virt(pa));
}
