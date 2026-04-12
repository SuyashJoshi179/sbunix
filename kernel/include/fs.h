#ifndef FS_H
#define FS_H

#include <stdint.h>

#define BSIZE       512          /* block size = VirtIO sector size */
#define FSMAGIC     0x10203040
#define NDIRECT     12
#define NBUF        32           /* buffer-cache slots */
#define NINODE      32           /* in-memory inode table size */
#define MAXFILE     (NDIRECT)    /* max blocks per file (direct only for now) */
#define DIRSIZ      14           /* max filename length */

/* ------------------------------------------------------------------ */
/* On-disk structures (must match mkfs.c exactly)                      */
/* ------------------------------------------------------------------ */

/* Block 0: boot (unused)   Block 1: superblock */
struct superblock {
    uint32_t magic;
    uint32_t size;        /* total blocks */
    uint32_t nblocks;     /* data blocks */
    uint32_t ninodes;
    uint32_t inodestart;  /* first inode block */
    uint32_t bmapstart;   /* first bitmap block */
};

/* On-disk inode (72 bytes) */
struct dinode {
    uint16_t type;              /* 0=free 1=dir 2=file */
    uint16_t nlink;
    uint32_t size;              /* bytes */
    uint32_t addrs[NDIRECT];   /* direct block addresses */
    uint32_t reserved[4];
};

/* Directory entry */
struct dirent {
    uint16_t inum;
    char     name[DIRSIZ];
};

/* ------------------------------------------------------------------ */
/* Buffer cache (bio.c)                                                */
/* ------------------------------------------------------------------ */

struct buf {
    int      valid;
    int      disk;
    uint32_t blockno;
    uint8_t  data[BSIZE];
};

void          binit(void);
struct buf   *bread(uint32_t blockno);
void          bwrite(struct buf *b);
void          brelse(struct buf *b);

/* ------------------------------------------------------------------ */
/* In-memory inode                                                     */
/* ------------------------------------------------------------------ */

struct inode {
    uint32_t dev;       /* always 0 for our single disk */
    uint32_t inum;
    int      ref;       /* reference count */
    int      valid;     /* dinode fields loaded from disk? */

    /* copy of on-disk dinode fields */
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t addrs[NDIRECT];
};

/* ------------------------------------------------------------------ */
/* File system layer (fs.c)                                            */
/* ------------------------------------------------------------------ */

void          fs_init(void);

/* inode management */
struct inode *ialloc(uint16_t type);
struct inode *iget(uint32_t inum);
void          iput(struct inode *ip);
void          iupdate(struct inode *ip);

/* file I/O */
int           readi(struct inode *ip, char *dst, uint32_t off, uint32_t n);
int           writei(struct inode *ip, char *src, uint32_t off, uint32_t n);

/* directory */
struct inode *dirlookup(struct inode *dp, const char *name, uint32_t *poff);
int           dirlink(struct inode *dp, const char *name, uint32_t inum);

/* path resolution */
struct inode *namei(const char *path);

/* low-level helpers (still used by tests) */
void          read_dinode(uint32_t inum, struct dinode *dip);
uint32_t      balloc(void);
void          fs_test_self(void);

#endif /* FS_H */
