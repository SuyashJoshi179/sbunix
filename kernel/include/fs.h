#ifndef FS_H
#define FS_H

#include <stdint.h>

#define BSIZE 512              // Block size (matches VirtIO sector size)
#define FSMAGIC 0x10203040     // File system magic number
#define NDIRECT 12             // Number of direct data blocks per inode
#define NBUF 32                // Number of buffer cache slots

// On-disk sector 0: Boot block (unused)
// On-disk sector 1: Superblock
struct superblock {
    uint32_t magic;        // Must be FSMAGIC
    uint32_t size;         // Total number of blocks in the disk image
    uint32_t nblocks;      // Number of data blocks
    uint32_t ninodes;      // Number of inodes
    uint32_t inodestart;   // Block number where inode table begins
    uint32_t bmapstart;    // Block number where free block bitmap begins
};

// On-disk Inode structure (exactly 64 bytes for alignment)
struct dinode {
    uint16_t type;         // File type (0: free, 1: directory, 2: file)
    uint16_t nlink;        // Number of links to this inode in file system
    uint32_t size;         // File size in bytes
    uint32_t addrs[NDIRECT]; // Direct data block addresses
    uint32_t reserved[4];  // Padding to reach 64 bytes
};

// Directory entry
struct dirent {
    uint16_t inum;         // Inode number
    char name[14];         // File name string
};

// --- Buffer Cache (bio.c) Interface ---
struct buf {
    int valid;             // Data has been read from disk
    int disk;              // Dirty bit (data needs to be written to disk)
    uint32_t blockno;      // Block number on disk
    uint8_t data[BSIZE];   // Actual content of the block
};

void binit(void);
struct buf* bread(uint32_t blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);

// --- File System (fs.c) Interface ---
void fs_init(void);
void read_dinode(uint32_t inum, struct dinode *dip);
uint32_t balloc(void);
void fs_test_self(void);

#endif