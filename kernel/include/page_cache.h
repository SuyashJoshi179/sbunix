#pragma once
#include <stdint.h>

struct inode;

#define PCACHE_NSLOTS 64
#define PCACHE_PGSZ   4096

struct pcache_page {
    struct inode *ip;          /* NULL = free slot */
    uint64_t      pgidx;       /* page index within file */
    void         *page;        /* PCACHE_PGSZ-byte physical page */
    int           refcnt;
    int           dirty;
    int           valid;       /* readpage filled successfully */
    struct pcache_page *prev, *next;  /* LRU doubly-linked */
};

void pcache_init(void);
int  pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out);
/* Drop one refcnt. On hit-zero the page is moved to the MRU head so a
 * recently-released page survives the next eviction wave; uninit slots
 * remain at the LRU tail and are evicted first (same policy as bio.c). */
void pcache_put(struct pcache_page *p);
int  pcache_flush_inode(struct inode *ip);
void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len);
