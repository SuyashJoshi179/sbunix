#pragma once
#include <stdint.h>

#define NBUF   32    /* number of cached blocks                    */
#define BSIZE  512   /* bytes per disk block (one VirtIO sector)   */

struct buf {
    int        valid;          /* data has been read from disk     */
    int        dirty;          /* needs to be written to disk      */
    int        refcnt;         /* number of active holders         */
    uint32_t   blockno;
    uint8_t    data[BSIZE];
    struct buf *prev;
    struct buf *next;          /* LRU doubly-linked list           */
};

void        binit(void);
struct buf *bread(uint32_t blockno);  /* pin + read; returns with refcnt ≥ 1 */
void        bwrite(struct buf *b);    /* flush to disk                        */
void        brelse(struct buf *b);    /* unpin; moves to LRU head             */
