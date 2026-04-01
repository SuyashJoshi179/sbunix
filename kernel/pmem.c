#include <pmem.h>
#include <string.h>
#include <vmem.h>

struct free_page {
    unsigned long next_phys;
};

static unsigned long freelist_phys = 0;

void pmem_init(void *start, void *end) {
    unsigned long p = page_round_up((unsigned long)start);
    while (p + PAGE_SIZE <= (unsigned long)end) {
        page_free((void *)p);
        p += PAGE_SIZE;
    }
}

void *page_alloc(void) {
    if (freelist_phys == 0) return 0;

    unsigned long page_phys = freelist_phys;
    struct free_page *page = (struct free_page *)phys_to_virt(page_phys);
    freelist_phys = page->next_phys;

    memset((void *)phys_to_virt(page_phys), 0, PAGE_SIZE);
    return (void *)phys_to_virt(page_phys);
}

void page_free(void *page) {
    unsigned long page_phys = virt_to_phys((unsigned long)page);
    struct free_page *node = (struct free_page *)phys_to_virt(page_phys);

    node->next_phys = freelist_phys;
    freelist_phys = page_phys;
}
