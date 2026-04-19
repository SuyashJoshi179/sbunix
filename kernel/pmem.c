#include <page_ref.h>
#include <pmem.h>
#include <printk.h>
#include <string.h>
#include <vmem.h>

// The freelist stores whatever addresses were passed to page_free().
//
// During early boot (before vmem_init), freelist contains physical addresses
// that are accessible via identity mappings.
//
// pmem_rebase(offset) must be called inside vmem_init() BEFORE identity
// mappings are cleared.  It walks the freelist and adds offset to every
// node pointer, converting physical → kernel virtual addresses.  After
// that, page_alloc() returns kernel virtual addresses and page_free()
// expects kernel virtual addresses.

struct free_page {
    struct free_page *next;
};

static struct free_page *freelist = 0;

void pmem_init(void *start, void *end) {
    unsigned long p = page_round_up((unsigned long)start);
    while (p + PAGE_SIZE <= (unsigned long)end) {
        page_free((void *)p);
        p += PAGE_SIZE;
    }
}

// Rebase all freelist node pointers by adding offset.
// Call once, after the high-half switch, before clearing identity maps.
void pmem_rebase(unsigned long offset) {
    // Adjust the head pointer
    if (freelist)
        freelist = (struct free_page *)((unsigned long)freelist + offset);

    // Walk via adjusted pointers and fix each ->next
    struct free_page *p = freelist;
    while (p) {
        if (p->next)
            p->next = (struct free_page *)((unsigned long)p->next + offset);
        p = p->next;
    }
}

extern int page_refs_ready;

void *page_alloc(void) {
    if (!freelist) return 0;
    struct free_page *p = freelist;
    freelist = p->next;

    unsigned long pa = virt_to_phys((unsigned long)p);
    /* Invariant: freelist nodes are page-aligned RAM pages only. */
    if ((((unsigned long)p) & (PAGE_SIZE - 1)) != 0)
        panic("page_alloc: unaligned freelist entry");
    if (pa < KERN_BASE || pa >= PHYMEM_END)
        panic("page_alloc: freelist entry out of range");

    memset(p, 0, PAGE_SIZE);
    if (page_refs_ready)
        page_ref_set(virt_to_phys((unsigned long)p), 1);
    return p;
}

void page_free(void *page) {
    /* Invariant: only page-aligned RAM pages can re-enter freelist. */
    if ((((unsigned long)page) & (PAGE_SIZE - 1)) != 0)
        panic("page_free: unaligned page");
    unsigned long pa = virt_to_phys((unsigned long)page);
    if (pa < KERN_BASE || pa >= PHYMEM_END)
        panic("page_free: page out of range");

    if (page_refs_ready) {
        unsigned char ref = page_ref_get(pa);
        if (ref != 1 && ref != 0)
            panic("page_free: refcount != 1");
        page_ref_set(pa, 0);
    }
    struct free_page *p = (struct free_page *)page;
    p->next = freelist;
    freelist = p;
}

// Count free pages by walking the freelist.
// Used by kernel selftests to detect leaks.
unsigned long pmem_free_count(void) {
    unsigned long n = 0;
    for (struct free_page *p = freelist; p; p = p->next)
        n++;
    return n;
}
