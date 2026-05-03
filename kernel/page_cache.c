#include <page_cache.h>
#include <pmem.h>
#include <inode.h>
#include <printk.h>
#include <string.h>
#include <errno.h>
#include <riscv.h>

static struct pcache_page slots[PCACHE_NSLOTS];
static struct pcache_page lru_head;          /* sentinel */
static int pcache_inited = 0;

/* Same IRQ-off pattern as bio.c. */
static int      pcache_depth = 0;
static uint64_t pcache_saved_sie = 0;

static inline void pcache_lock(void) {
    uint64_t s = read_sstatus();
    write_sstatus(s & ~SSTATUS_SIE);
    if (pcache_depth++ == 0) pcache_saved_sie = s & SSTATUS_SIE;
}

static inline void pcache_unlock(void) {
    if (--pcache_depth == 0 && pcache_saved_sie)
        write_sstatus(read_sstatus() | SSTATUS_SIE);
}

void pcache_init(void) {
    if (pcache_inited) return;
    lru_head.prev = &lru_head;
    lru_head.next = &lru_head;
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        slots[i].ip     = 0;
        slots[i].pgidx  = 0;
        slots[i].refcnt = 0;
        slots[i].dirty  = 0;
        slots[i].valid  = 0;
        slots[i].page   = page_alloc();
        if (!slots[i].page) panic("pcache_init: page_alloc failed");
        slots[i].next = &lru_head;
        slots[i].prev = lru_head.prev;
        lru_head.prev->next = &slots[i];
        lru_head.prev = &slots[i];
    }
    pcache_inited = 1;
    printk("pcache: %d slots x %d bytes ready\n",
           PCACHE_NSLOTS, PCACHE_PGSZ);
}

static void lru_unlink(struct pcache_page *p) {
    p->prev->next = p->next;
    p->next->prev = p->prev;
}

static void lru_to_head(struct pcache_page *p) {
    /* head = MRU; tail = LRU candidate */
    p->next = lru_head.next;
    p->prev = &lru_head;
    lru_head.next->prev = p;
    lru_head.next = p;
}

/* Find existing slot for (ip, pgidx). NULL if absent. Caller holds lock. */
static struct pcache_page *pcache_lookup(struct inode *ip, uint64_t pgidx) {
    for (struct pcache_page *p = lru_head.next; p != &lru_head; p = p->next) {
        if (p->ip == ip && p->pgidx == pgidx && p->valid)
            return p;
    }
    return 0;
}

/* Find a victim slot to reuse. Returns NULL if all pinned/dirty.
 * Phase B writepage flushing of dirty victims is added in Task 7;
 * for now we only evict clean refcnt==0 pages. Caller holds lock. */
static struct pcache_page *pcache_evict(void) {
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt == 0 && !p->dirty) {
            return p;
        }
    }
    return 0;
}

int pcache_get(struct inode *ip, uint64_t pgidx, struct pcache_page **out) {
    pcache_lock();
    struct pcache_page *p = pcache_lookup(ip, pgidx);
    if (p) {
        p->refcnt++;
        lru_unlink(p);
        lru_to_head(p);
        pcache_unlock();
        *out = p;
        return 0;
    }
    p = pcache_evict();
    if (!p) {
        pcache_unlock();
        return -ENOMEM;
    }
    p->ip     = ip;
    p->pgidx  = pgidx;
    p->refcnt = 1;
    p->dirty  = 0;
    p->valid  = 0;
    lru_unlink(p);
    lru_to_head(p);
    pcache_unlock();
    /* readpage call deferred to Task 3; for now just zero the page. */
    memset(p->page, 0, PCACHE_PGSZ);
    p->valid = 1;
    *out = p;
    return 0;
}

void pcache_put(struct pcache_page *p) {
    if (!p) return;
    pcache_lock();
    if (p->refcnt > 0) p->refcnt--;
    if (p->refcnt == 0) {
        /* Move to MRU head so recently-used pages survive; truly untouched
         * slots (inserted at tail during init) remain at the LRU tail. */
        lru_unlink(p);
        lru_to_head(p);
    }
    pcache_unlock();
}

int pcache_flush_inode(struct inode *ip) { (void)ip; return 0; }

void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len) {
    (void)ip; (void)off; (void)len;
}
