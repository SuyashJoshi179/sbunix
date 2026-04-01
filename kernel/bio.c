#include <fs.h>
#include <virtio_blk.h>
#include <string.h>
#include <printk.h>

static struct buf cache[NBUF];

// Initialize buffer cache
void binit(void) {
    for (int i = 0; i < NBUF; i++) {
        cache[i].valid = 0;
        cache[i].disk = 0;
    }
}

// Read a block from disk into cache and return the buffer
struct buf* bread(uint32_t blockno) {
    // 1. Check if block is already cached
    for (int i = 0; i < NBUF; i++) {
        if (cache[i].valid && cache[i].blockno == blockno) {
            return &cache[i];
        }
    }
    
    // 2. If not cached, find a free slot (simple replacement policy)
    for (int i = 0; i < NBUF; i++) {
        if (!cache[i].valid) {
            cache[i].blockno = blockno;
            virtio_blk_read(blockno, cache[i].data);
            cache[i].valid = 1;
            cache[i].disk = 0;
            return &cache[i];
        }
    }
    
    printk("Buffer cache full! Failed to bread block %d\n", blockno);
    return 0;
}

// Synchronize buffer content back to disk
void bwrite(struct buf *b) {
    if (!b->valid) return;
    virtio_blk_write(b->blockno, b->data);
    b->disk = 0;
}

// Release buffer, writing back to disk if dirty
void brelse(struct buf *b) {
    if (b->disk) {
        bwrite(b);
    }
    // In a real OS, we would release a sleep-lock here.
}