#include <pmem.h>

struct free_page {
    struct free_page *next;
};

static struct free_page *freelist = 0;

static inline unsigned long page_round_up(unsigned long addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void pmem_init(void *start, void *end) {
    unsigned long p = page_round_up((unsigned long)start);
    while (p + PAGE_SIZE <= (unsigned long)end) {
        page_free((void *)p);
        p += PAGE_SIZE;
    }
}

void *page_alloc(void) {
    if (!freelist) return 0;
    struct free_page *p = freelist;
    freelist = p->next;
    for (int i = 0; i < PAGE_SIZE / 8; i++)
        ((unsigned long *)p)[i] = 0;
    return p;
}

void page_free(void *page) {
    struct free_page *p = (struct free_page *)page;
    p->next = freelist;
    freelist = p;
}
