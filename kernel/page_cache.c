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
 * Caller holds the lock on entry. We may drop and re-acquire it across
 * the writepage call because sbfs_writepage_locked acquires the bio
 * cache lock and the log lock — neither is reentrant under our IRQ-off
 * section. */
static struct pcache_page *pcache_evict(void) {
    /* First pass: clean unpinned. */
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt == 0 && !p->dirty) return p;
    }
    /* Second pass: dirty unpinned — flush, then reuse. */
    for (struct pcache_page *p = lru_head.prev; p != &lru_head; p = p->prev) {
        if (p->refcnt != 0) continue;
        if (!p->dirty) continue;
        if (!p->ip || !p->ip->ops || !p->ip->ops->writepage) {
            p->dirty = 0;
            return p;
        }
        struct inode *ip = p->ip;
        uint64_t pgidx = p->pgidx;
        /* Pin during flush so a parallel pcache_get can't reuse it. */
        p->refcnt++;
        pcache_unlock();
        extern int sbfs_writepage_locked(struct inode *, uint64_t,
                                         const void *);
        int rc = sbfs_writepage_locked(ip, pgidx, p->page);
        pcache_lock();
        p->refcnt--;
        if (rc == 0) p->dirty = 0;
        if (p->refcnt == 0 && !p->dirty) return p;
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
    /* Miss path: readpage if available, else zero-fill (for filesystems
     * without readpage we still serve a zero page so callers can detect
     * via valid flag; in practice generic_file_read only calls pcache
     * for inodes with readpage set). */
    int rc = 0;
    if (ip->ops && ip->ops->readpage) {
        rc = ip->ops->readpage(ip, pgidx, p->page);
    } else {
        memset(p->page, 0, PCACHE_PGSZ);
    }
    if (rc < 0) {
        /* Drop the slot — leave it ip=0 so next get re-fetches.
         * Refcnt stays 1 in the caller view; we instead treat this as
         * an immediate failure and unwind. */
        pcache_lock();
        p->ip = 0;
        p->pgidx = 0;
        p->refcnt = 0;
        p->valid = 0;
        /* Move back to tail so it's preferred for next reuse. */
        lru_unlink(p);
        p->next = &lru_head;
        p->prev = lru_head.prev;
        lru_head.prev->next = p;
        lru_head.prev = p;
        pcache_unlock();
        return rc;
    }
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

int pcache_flush_inode(struct inode *ip) {
    if (!ip) return 0;
    int busy = 0;
    pcache_lock();
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        struct pcache_page *p = &slots[i];
        if (p->ip != ip) continue;
        if (p->refcnt > 0) { busy = 1; continue; }
        if (p->dirty && p->ip->ops && p->ip->ops->writepage) {
            extern int sbfs_writepage_locked(struct inode *, uint64_t,
                                             const void *);
            p->refcnt++;
            pcache_unlock();
            sbfs_writepage_locked(p->ip, p->pgidx, p->page);
            pcache_lock();
            p->refcnt--;
            p->dirty = 0;
        }
        p->ip = 0;
        p->pgidx = 0;
        p->valid = 0;
    }
    pcache_unlock();
    return busy ? -EBUSY : 0;
}

void pcache_invalidate_range(struct inode *ip, uint64_t off, uint64_t len) {
    if (!ip || len == 0) return;
    uint64_t pg_start = off / PCACHE_PGSZ;
    uint64_t pg_end   = (off + len + PCACHE_PGSZ - 1) / PCACHE_PGSZ;
    pcache_lock();
    for (int i = 0; i < PCACHE_NSLOTS; i++) {
        struct pcache_page *p = &slots[i];
        if (p->ip != ip) continue;
        if (p->pgidx < pg_start || p->pgidx >= pg_end) continue;
        if (p->refcnt > 0) {
            /* Mapped page — leave it; bounds check at fault path will
             * surface SIGBUS for past-EOF accesses. Coherence with
             * concurrent fd-writes deferred to Phase D. */
            continue;
        }
        p->ip = 0;
        p->pgidx = 0;
        p->valid = 0;
        p->dirty = 0;
    }
    pcache_unlock();
}
