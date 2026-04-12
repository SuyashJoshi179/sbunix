#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Must match kernel/include/fs.h exactly */
#define BSIZE       512
#define FSMAGIC     0x10203040
#define NDIRECT     12
#define NINODES     200
#define DIRSIZ      14

struct superblock {
    uint32_t magic;
    uint32_t size;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t inodestart;
    uint32_t bmapstart;
};

struct dinode {
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT];
    uint32_t reserved[4];
};

struct dirent {
    uint16_t inum;
    char     name[DIRSIZ];
};

/* ---- disk layout (filled in by main) ---- */
static uint32_t inodestart, bmapstart, data_start, total_blocks;
static FILE *disk;

static void write_block(uint32_t blockno, void *data) {
    fseek(disk, (long)blockno * BSIZE, SEEK_SET);
    fwrite(data, BSIZE, 1, disk);
}

static void read_block(uint32_t blockno, void *buf) {
    fseek(disk, (long)blockno * BSIZE, SEEK_SET);
    if (fread(buf, BSIZE, 1, disk) != 1)
        memset(buf, 0, BSIZE);
}

static void bitmap_mark_used(uint32_t b) {
    char buf[BSIZE];
    uint32_t bmap_block = bmapstart + b / (BSIZE * 8);
    read_block(bmap_block, buf);
    buf[(b % (BSIZE * 8)) / 8] |= (uint8_t)(1 << (b % 8));
    write_block(bmap_block, buf);
}

/* Allocate the next free data block (simple linear scan). */
static uint32_t next_free_block;
static uint32_t balloc(void) {
    uint32_t b = next_free_block++;
    bitmap_mark_used(b);
    return b;
}

/* Write inode inum to disk. */
static void write_inode(uint32_t inum, struct dinode *di) {
    uint32_t ipb     = BSIZE / sizeof(struct dinode);
    uint32_t blockno = inodestart + inum / ipb;
    uint32_t off     = (inum % ipb) * sizeof(struct dinode);
    char buf[BSIZE];
    read_block(blockno, buf);
    memcpy(buf + off, di, sizeof(*di));
    write_block(blockno, buf);
}

/* Append dirent (inum, name) to directory inode *dp. */
static void dir_append(struct dinode *dp, uint32_t dir_data_blk,
                       uint16_t inum, const char *name) {
    uint32_t off = dp->size;
    /* ensure we stay within the first (and only) data block */
    if (off + sizeof(struct dirent) > BSIZE) {
        fprintf(stderr, "mkfs: root dir too large\n");
        exit(1);
    }
    char buf[BSIZE];
    read_block(dir_data_blk, buf);
    struct dirent de;
    memset(&de, 0, sizeof(de));
    de.inum = inum;
    strncpy(de.name, name, DIRSIZ);
    memcpy(buf + off, &de, sizeof(de));
    write_block(dir_data_blk, buf);
    dp->size += sizeof(struct dirent);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: mkfs <image> <size_mb>\n");
        return 1;
    }

    long mb = atol(argv[2]);
    if (mb <= 0) { fprintf(stderr, "invalid size\n"); return 1; }

    /* --- Layout --- */
    total_blocks             = (uint32_t)((mb * 1024L * 1024) / BSIZE);
    uint32_t ninodes         = NINODES;
    uint32_t inodes_per_blk  = BSIZE / (uint32_t)sizeof(struct dinode);
    uint32_t inode_blocks    = (ninodes + inodes_per_blk - 1) / inodes_per_blk;
    uint32_t bitmap_blocks   = (total_blocks + BSIZE * 8 - 1) / (BSIZE * 8);
    inodestart               = 2;
    bmapstart                = inodestart + inode_blocks;
    data_start               = bmapstart + bitmap_blocks;
    uint32_t nblocks         = total_blocks - data_start;
    next_free_block          = data_start;   /* first free data block */

    printf("mkfs: formatting %s (%ld MB, %u blocks)\n", argv[1], mb, total_blocks);
    printf("  block 1        : superblock\n");
    printf("  blocks %u-%u  : inodes\n", inodestart, bmapstart - 1);
    printf("  blocks %u-%u  : bitmap\n", bmapstart, data_start - 1);
    printf("  blocks %u+    : data (%u blocks)\n", data_start, nblocks);

    /* --- Create image --- */
    disk = fopen(argv[1], "wb+");
    if (!disk) { perror(argv[1]); return 1; }
    fseek(disk, (long)total_blocks * BSIZE - 1, SEEK_SET);
    fputc(0, disk);

    /* --- Superblock --- */
    char sb_buf[BSIZE];
    memset(sb_buf, 0, BSIZE);
    struct superblock sb = {
        .magic      = FSMAGIC,
        .size       = total_blocks,
        .nblocks    = nblocks,
        .ninodes    = ninodes,
        .inodestart = inodestart,
        .bmapstart  = bmapstart,
    };
    memcpy(sb_buf, &sb, sizeof(sb));
    write_block(1, sb_buf);

    /* --- Mark metadata blocks used --- */
    for (uint32_t b = 0; b < data_start; b++)
        bitmap_mark_used(b);

    /* --- Root directory (inode 1, type=1) --- */
    uint32_t root_data_blk = balloc();   /* allocate one data block */

    struct dinode root;
    memset(&root, 0, sizeof(root));
    root.type  = 1;   /* directory */
    root.nlink = 1;
    root.size  = 0;
    root.addrs[0] = root_data_blk;

    /* Add "." and ".." entries */
    dir_append(&root, root_data_blk, 1, ".");
    dir_append(&root, root_data_blk, 1, "..");

    write_inode(1, &root);
    printf("mkfs: root dir at inode 1, data block %u\n", root_data_blk);

    fclose(disk);
    printf("mkfs: done\n");
    return 0;
}
