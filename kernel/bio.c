/*
 * bio.c — Buffer cache (LRU, NBUF=32 slots)
 *
 * Provides a read-through, write-back cache for 512-byte disk blocks.
 * All operations disable interrupts for the critical section — we are
 * single-hart and have no locks, so IRQs-off is sufficient.
 *
 * LRU invariant: the doubly-linked list is ordered most-recently-used
 * (head) to least-recently-used (tail).  brelse() moves the released
 * buffer to the head.  When we need a new slot we evict from the tail.
 */

#include <bio.h>
#include <drivers/virtio.h>
#include <riscv.h>
#include <printk.h>
#include <string.h>

static struct buf bcache[NBUF];
static struct buf bhead;   /* sentinel head of the LRU doubly-linked list */

static inline void cache_lock(void) {
    write_sstatus(read_sstatus() & ~SSTATUS_SIE);
}
static inline void cache_unlock(void) {
    write_sstatus(read_sstatus() | SSTATUS_SIE);
}

/* -----------------------------------------------------------------------
 * binit — set up the circular doubly-linked list
 * ----------------------------------------------------------------------- */
void binit(void) {
    bhead.prev = &bhead;
    bhead.next = &bhead;

    for (int i = 0; i < NBUF; i++) {
        bcache[i].valid  = 0;
        bcache[i].dirty  = 0;
        bcache[i].refcnt = 0;
        bcache[i].blockno = 0;
        /* Insert at the tail of the LRU list (all buffers start as cold). */
        bcache[i].next = &bhead;
        bcache[i].prev = bhead.prev;
        bhead.prev->next = &bcache[i];
        bhead.prev = &bcache[i];
    }
}

/* -----------------------------------------------------------------------
 * bget — find or allocate a buffer for blockno (IRQs must be off).
 *
 * First, search the list for an existing cached buffer.
 * If not found, evict the LRU clean buffer from the tail.
 * ----------------------------------------------------------------------- */
static struct buf *bget(uint32_t blockno) {
    /* 1. Hit: already in the cache? */
    for (struct buf *b = bhead.next; b != &bhead; b = b->next) {
        if (b->blockno == blockno && b->valid) {
            b->refcnt++;
            return b;
        }
    }

    /* 2. Miss: evict the LRU clean buffer (tail → head search). */
    for (struct buf *b = bhead.prev; b != &bhead; b = b->prev) {
        if (b->refcnt == 0 && !b->dirty) {
            b->blockno = blockno;
            b->valid   = 0;
            b->dirty   = 0;
            b->refcnt  = 1;
            /* Move to the MRU position (just after head). */
            b->prev->next = b->next;
            b->next->prev = b->prev;
            b->next = bhead.next;
            b->prev = &bhead;
            bhead.next->prev = b;
            bhead.next = b;
            return b;
        }
    }

    panic("bio: no free buffers (all pinned or dirty)");
    return 0;
}

/* -----------------------------------------------------------------------
 * bread — return a pinned, valid buffer for blockno.
 * Reads from disk if the buffer is cold (valid == 0).
 * ----------------------------------------------------------------------- */
struct buf *bread(uint32_t blockno) {
    cache_lock();
    struct buf *b = bget(blockno);
    cache_unlock();

    if (!b->valid) {
        virtio_disk_rw(blockno, b->data, 0);  /* read */
        b->valid = 1;
    }
    return b;
}

/* -----------------------------------------------------------------------
 * bwrite — flush a dirty buffer to disk immediately.
 * Called by the log to commit transactions.
 * ----------------------------------------------------------------------- */
void bwrite(struct buf *b) {
    virtio_disk_rw(b->blockno, b->data, 1);  /* write */
    b->dirty = 0;
}

/* -----------------------------------------------------------------------
 * brelse — unpin a buffer.  Moves it to the MRU head so it survives
 * longer before eviction.
 * ----------------------------------------------------------------------- */
void brelse(struct buf *b) {
    cache_lock();
    b->refcnt--;
    if (b->refcnt == 0) {
        /* Move to MRU head. */
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = bhead.next;
        b->prev = &bhead;
        bhead.next->prev = b;
        bhead.next = b;
    }
    cache_unlock();
}
