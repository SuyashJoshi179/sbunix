/*
 * mkfs.c — creates an sbfs v1 disk image
 *
 * On-disk layout (all blocks are BSIZE=512 bytes):
 *   block 0        : boot block (reserved, zeroed)
 *   block 1        : superblock
 *   block 2..17    : write-ahead log (LOGSIZE=16 blocks)
 *   block 18..49   : inode table (NINODES=256, 8 per block → 32 blocks)
 *   block 50       : block bitmap (1 block covers up to 4096 data blocks)
 *   block 51..1050 : data blocks (NDATABLOCKS=1000)
 *
 * The root directory (inode 1) is created with two entries: "." and "..".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* -----------------------------------------------------------------------
 * On-disk constants — must match kernel/include/sbfs.h exactly
 * ----------------------------------------------------------------------- */
#define BSIZE         512
#define MAGIC         0x53425631u   /* "SBV1" */
#define NINODES       256
#define LOGSIZE       16
#define NDATABLOCKS   1000
#define NDIRECT       12
#define DIRSIZ        14
#define ROOTINUM      1             /* inode number of the root directory */

/* Block layout */
#define BOOT_BLOCK    0
#define SB_BLOCK      1
#define LOG_START     2
#define INODE_START   (LOG_START + LOGSIZE)         /* block 18 */
#define INODE_BLOCKS  (NINODES / 8)                 /* 32 blocks (8 inodes/block) */
#define BMAP_BLOCK    (INODE_START + INODE_BLOCKS)  /* block 50 */
#define DATA_START    (BMAP_BLOCK + 1)              /* block 51 */
#define NBLOCKS       (DATA_START + NDATABLOCKS)    /* total blocks in image */

/* -----------------------------------------------------------------------
 * Superblock
 * ----------------------------------------------------------------------- */
struct sb_superblock {
    uint32_t magic;
    uint32_t size;        /* total blocks in image */
    uint32_t nblocks;     /* number of data blocks */
    uint32_t ninodes;
    uint32_t nlog;
    uint32_t logstart;
    uint32_t inodestart;
    uint32_t bmapstart;
};

/* -----------------------------------------------------------------------
 * On-disk inode (exactly 64 bytes → 8 per 512-byte block)
 *   type(2) + nlink(2) + size(4) + mtime(8) + addrs[12](48) = 64
 * ----------------------------------------------------------------------- */
struct sb_dinode {
    uint16_t type;         /* 0=free, 1=file, 2=dir */
    uint16_t nlink;
    uint32_t size;
    uint64_t mtime;        /* last-modification time (unused in v1) */
    uint32_t addrs[NDIRECT];
};

/* -----------------------------------------------------------------------
 * Directory entry (16 bytes → 32 per block)
 * ----------------------------------------------------------------------- */
struct sb_dirent {
    uint16_t inum;
    char     name[DIRSIZ];
};

/* -----------------------------------------------------------------------
 * Image writing helpers
 * ----------------------------------------------------------------------- */
static FILE *img;
static uint8_t bitmap[BSIZE];
static int next_data_block = DATA_START;   /* next allocatable data block */

static void write_block(uint32_t bno, const void *data) {
    if (fseek(img, (long)bno * BSIZE, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    if (fwrite(data, BSIZE, 1, img) != 1) {
        perror("fwrite");
        exit(1);
    }
}

static void read_block(uint32_t bno, void *data) {
    if (fseek(img, (long)bno * BSIZE, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    if (fread(data, BSIZE, 1, img) != 1) {
        perror("fread");
        exit(1);
    }
}

/* Allocate a data block: mark it used in the bitmap and return its number. */
static uint32_t balloc_data(void) {
    uint32_t bno = next_data_block++;
    if (bno >= DATA_START + NDATABLOCKS) {
        fprintf(stderr, "mkfs: out of data blocks\n");
        exit(1);
    }
    /* Mark in the in-memory bitmap (relative to DATA_START). */
    uint32_t rel = bno - DATA_START;
    bitmap[rel / 8] |= (1u << (rel % 8));
    return bno;
}

/* Write an inode to the inode table. */
static void write_inode(uint32_t inum, const struct sb_dinode *d) {
    uint32_t block  = INODE_START + inum / 8;
    uint32_t offset = (inum % 8) * sizeof(struct sb_dinode);
    uint8_t  blk[BSIZE];
    read_block(block, blk);
    memcpy(blk + offset, d, sizeof(*d));
    write_block(block, blk);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: mkfs <image> [size_mb]\n");
        return 1;
    }
    /* size_mb (argv[2]) is accepted for compatibility with the master
     * Makefile but ignored: sbfs v1 has a fixed on-disk layout. */

    img = fopen(argv[1], "w+b");
    if (!img) {
        perror(argv[1]);
        return 1;
    }

    /* 1. Zero the entire image. */
    uint8_t zero[BSIZE];
    memset(zero, 0, BSIZE);
    for (int i = 0; i < NBLOCKS; i++)
        write_block(i, zero);

    /* 2. Write the superblock. */
    uint8_t sb_block[BSIZE];
    memset(sb_block, 0, BSIZE);
    struct sb_superblock *sb = (struct sb_superblock *)sb_block;
    sb->magic      = MAGIC;
    sb->size       = NBLOCKS;
    sb->nblocks    = NDATABLOCKS;
    sb->ninodes    = NINODES;
    sb->nlog       = LOGSIZE;
    sb->logstart   = LOG_START;
    sb->inodestart = INODE_START;
    sb->bmapstart  = BMAP_BLOCK;
    write_block(SB_BLOCK, sb_block);

    /* 3. Allocate the root directory inode (inum = ROOTINUM = 1).
     *    The root dir always has 2 fixed entries: "." and ".."
     *    (both pointing to inode 1 for the root). */
    uint32_t root_data = balloc_data();  /* first data block of root dir */

    uint8_t dir_block[BSIZE];
    memset(dir_block, 0, BSIZE);
    struct sb_dirent *de = (struct sb_dirent *)dir_block;

    /* Entry 0: "." → root itself */
    de[0].inum = ROOTINUM;
    memcpy(de[0].name, ".", 2);

    /* Entry 1: ".." → root itself (root has no parent) */
    de[1].inum = ROOTINUM;
    memcpy(de[1].name, "..", 3);

    write_block(root_data, dir_block);

    struct sb_dinode root_ino;
    memset(&root_ino, 0, sizeof(root_ino));
    root_ino.type   = 2;               /* directory */
    root_ino.nlink  = 2;               /* "." and parent reference */
    root_ino.size   = 2 * sizeof(struct sb_dirent);
    root_ino.addrs[0] = root_data;
    write_inode(ROOTINUM, &root_ino);

    /* 4. Write the block bitmap. */
    /* Blocks 0..DATA_START-1 (overhead) are always "allocated" so the
     * allocator never hands them out as data blocks. Mark them used. */
    for (int i = 0; i < (int)DATA_START; i++) {
        /* These bits are in the "relative" space: they would be negative
         * (i < DATA_START), so we don't actually need to mark them in the
         * bitmap — the allocator counts from DATA_START.  The bitmap block
         * only covers data blocks (indices 0..NDATABLOCKS-1 relative).
         * Only mark the ones we actually allocated. */
    }
    /* bitmap[] was filled by balloc_data() for each allocated data block. */
    write_block(BMAP_BLOCK, bitmap);

    fclose(img);
    printf("mkfs: created %s (%d blocks, %d inodes, %d data blocks)\n",
           argv[1], NBLOCKS, NINODES, NDATABLOCKS);
    return 0;
}
