#include <pmem.h>
#include <vmem.h>
#include <string.h>

// The freelist stores physical addresses internally.
// page_alloc() returns a kernel virtual address (phys + mem_offset).
// page_free() accepts a kernel virtual address.
//
// During early boot mem_offset = 0, so virtual == physical and
// identity mappings are still active — no behaviour change until
// vmem_init() sets mem_offset = KVMEM_OFFSET.

struct free_page {
    struct free_page *next;   // physical pointer stored in freelist node
};

static struct free_page *freelist = 0;  // physical address of first free page

void pmem_init(void *start, void *end) {
    unsigned long p = page_round_up((unsigned long)start);
    while (p + PAGE_SIZE <= (unsigned long)end) {
        page_free((void *)p);   // start/end are physical at boot (mem_offset=0)
        p += PAGE_SIZE;
    }
}

// Returns kernel virtual address of a zeroed 4KB page, or NULL on OOM.
void *page_alloc(void) {
    if (!freelist) return 0;
    struct free_page *p = freelist;
    freelist = p->next;
    // p is a physical address; add mem_offset to get kernel virtual address
    void *va = (void *)((unsigned long)p + mem_offset);
    memset(va, 0, PAGE_SIZE);
    return va;
}

// Accepts a kernel virtual address returned by page_alloc().
void page_free(void *page) {
    // Convert virtual → physical for freelist storage
    struct free_page *p = (struct free_page *)((unsigned long)page - mem_offset);
    p->next = freelist;
    freelist = p;
}
