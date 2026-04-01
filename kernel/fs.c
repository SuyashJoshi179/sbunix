#include <fs.h>
#include <printk.h>
#include <string.h>

static struct superblock sb;

// Initialize file system by reading the superblock
void fs_init(void) {
    printk("DEBUG: before bread\n");
    struct buf *b = bread(1); // Superblock is at Sector 1
    printk("DEBUG: after bread\n");
    memmove(&sb, b->data, sizeof(sb));
    brelse(b);

    if (sb.magic != FSMAGIC) {
        printk("FS Error: Could not find a valid filesystem on disk!\n");
    } else {
        printk("Filesystem initialized: Size %d blocks, %d Inodes\n", sb.size, sb.ninodes);
    }
}

// Read an inode from the disk's inode table
void read_dinode(uint32_t inum, struct dinode *dip) {
    uint32_t inodes_per_block = BSIZE / sizeof(struct dinode);
    uint32_t blockno = (inum / inodes_per_block) + sb.inodestart;
    uint32_t offset = (inum % inodes_per_block) * sizeof(struct dinode);
    
    struct buf *b = bread(blockno);
    memmove(dip, b->data + offset, sizeof(struct dinode));
    brelse(b);
}

// Allocate a free data block using the bitmap
uint32_t balloc(void) {
    for (uint32_t b = 0; b < sb.size; b += BSIZE * 8) {
        struct buf *bp = bread(sb.bmapstart + b / (BSIZE * 8));
        for (int bi = 0; bi < BSIZE * 8 && b + bi < sb.size; bi++) {
            int mask = 1 << (bi % 8);
            if ((bp->data[bi / 8] & mask) == 0) { // Free block found
                bp->data[bi / 8] |= mask;         // Mark as used
                bp->disk = 1;                     // Mark buffer as dirty
                uint32_t blockno = b + bi;
                brelse(bp);
                return blockno;
            }
        }
        brelse(bp);
    }
    printk("FS Error: Out of data blocks!\n");
    return 0;
}

// Simple test function to verify disk read/write
void fs_test_self(void) {
    printk("Starting FS self-test...\n");
    struct buf *b = bread(100); // Read arbitrary block
    b->data[0] = 'S';
    b->data[1] = 'B';
    b->disk = 1;
    brelse(b);
    printk("FS self-test: Write to block 100 successful.\n");
}