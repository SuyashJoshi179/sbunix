#include <fs.h>
#include <printk.h>
#include <string.h>

static struct superblock sb;
static struct inode      icache[NINODE];

/* ------------------------------------------------------------------ */
/* Superblock                                                          */
/* ------------------------------------------------------------------ */

void fs_init(void) {
    struct buf *b = bread(1);
    memmove(&sb, b->data, sizeof(sb));
    brelse(b);

    if (sb.magic != FSMAGIC) {
        printk("FS Error: invalid magic 0x%x\n", sb.magic);
        return;
    }
    printk("Filesystem initialized: Size %d blocks, %d Inodes\n",
           sb.size, sb.ninodes);
}

/* ------------------------------------------------------------------ */
/* Bitmap                                                              */
/* ------------------------------------------------------------------ */

uint32_t balloc(void) {
    for (uint32_t b = 0; b < sb.size; b += BSIZE * 8) {
        struct buf *bp = bread(sb.bmapstart + b / (BSIZE * 8));
        for (int bi = 0; bi < BSIZE * 8 && b + bi < sb.size; bi++) {
            int mask = 1 << (bi % 8);
            if ((bp->data[bi / 8] & mask) == 0) {
                bp->data[bi / 8] |= mask;
                bp->disk = 1;
                uint32_t blockno = b + bi;
                brelse(bp);
                return blockno;
            }
        }
        brelse(bp);
    }
    printk("FS Error: out of data blocks\n");
    return 0;
}

static void bfree(uint32_t b) {
    struct buf *bp = bread(sb.bmapstart + b / (BSIZE * 8));
    int bi   = b % (BSIZE * 8);
    int mask = 1 << (bi % 8);
    bp->data[bi / 8] &= ~mask;
    bp->disk = 1;
    brelse(bp);
}

/* ------------------------------------------------------------------ */
/* On-disk inode helpers                                               */
/* ------------------------------------------------------------------ */

void read_dinode(uint32_t inum, struct dinode *dip) {
    uint32_t ipb     = BSIZE / sizeof(struct dinode);
    uint32_t blockno = (inum / ipb) + sb.inodestart;
    uint32_t off     = (inum % ipb) * sizeof(struct dinode);

    struct buf *b = bread(blockno);
    memmove(dip, b->data + off, sizeof(struct dinode));
    brelse(b);
}

static void write_dinode(uint32_t inum, struct dinode *dip) {
    uint32_t ipb     = BSIZE / sizeof(struct dinode);
    uint32_t blockno = (inum / ipb) + sb.inodestart;
    uint32_t off     = (inum % ipb) * sizeof(struct dinode);

    struct buf *b = bread(blockno);
    memmove(b->data + off, dip, sizeof(struct dinode));
    b->disk = 1;
    brelse(b);
}

/* ------------------------------------------------------------------ */
/* In-memory inode management                                          */
/* ------------------------------------------------------------------ */

struct inode *ialloc(uint16_t type) {
    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        struct dinode di;
        read_dinode(inum, &di);
        if (di.type == 0) {       /* free slot */
            memset(&di, 0, sizeof(di));
            di.type = type;
            write_dinode(inum, &di);
            return iget(inum);
        }
    }
    printk("FS Error: no free inodes\n");
    return 0;
}

struct inode *iget(uint32_t inum) {
    struct inode *empty = 0;

    for (int i = 0; i < NINODE; i++) {
        if (icache[i].ref > 0 && icache[i].inum == inum) {
            icache[i].ref++;
            return &icache[i];
        }
        if (empty == 0 && icache[i].ref == 0)
            empty = &icache[i];
    }

    if (empty == 0) {
        printk("FS Error: inode cache full\n");
        return 0;
    }

    empty->inum  = inum;
    empty->ref   = 1;
    empty->valid = 0;
    empty->dev   = 0;
    return empty;
}

/* Load dinode fields into in-memory inode (lazy, on first use). */
static void iload(struct inode *ip) {
    if (ip->valid) return;
    struct dinode di;
    read_dinode(ip->inum, &di);
    ip->type  = di.type;
    ip->nlink = di.nlink;
    ip->size  = di.size;
    for (int i = 0; i < NDIRECT; i++)
        ip->addrs[i] = di.addrs[i];
    ip->valid = 1;
}

void iupdate(struct inode *ip) {
    struct dinode di;
    read_dinode(ip->inum, &di);
    di.type  = ip->type;
    di.nlink = ip->nlink;
    di.size  = ip->size;
    for (int i = 0; i < NDIRECT; i++)
        di.addrs[i] = ip->addrs[i];
    write_dinode(ip->inum, &di);
}

void iput(struct inode *ip) {
    if (ip == 0) return;
    ip->ref--;
    if (ip->ref == 0 && ip->valid && ip->nlink == 0) {
        /* free all data blocks */
        iload(ip);
        for (int i = 0; i < NDIRECT; i++) {
            if (ip->addrs[i]) {
                bfree(ip->addrs[i]);
                ip->addrs[i] = 0;
            }
        }
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Block mapping                                                       */
/* ------------------------------------------------------------------ */

/* Return the disk block number for logical block bn in inode ip.
   Allocates a new block if allocate != 0 and the slot is empty. */
static uint32_t bmap(struct inode *ip, uint32_t bn, int allocate) {
    if (bn >= NDIRECT) {
        printk("FS Error: bmap out of range %d\n", bn);
        return 0;
    }
    if (ip->addrs[bn] == 0) {
        if (!allocate) return 0;
        ip->addrs[bn] = balloc();
        iupdate(ip);
    }
    return ip->addrs[bn];
}

/* ------------------------------------------------------------------ */
/* File I/O                                                            */
/* ------------------------------------------------------------------ */

int readi(struct inode *ip, char *dst, uint32_t off, uint32_t n) {
    iload(ip);

    if (off > ip->size) return 0;
    if (off + n > ip->size)
        n = ip->size - off;

    uint32_t total = 0;
    while (total < n) {
        uint32_t bn   = (off + total) / BSIZE;
        uint32_t boff = (off + total) % BSIZE;
        uint32_t blk  = bmap(ip, bn, 0);
        if (blk == 0) break;

        uint32_t chunk = BSIZE - boff;
        if (chunk > n - total) chunk = n - total;

        struct buf *b = bread(blk);
        memmove(dst + total, b->data + boff, chunk);
        brelse(b);
        total += chunk;
    }
    return (int)total;
}

int writei(struct inode *ip, char *src, uint32_t off, uint32_t n) {
    iload(ip);

    if (off + n > (uint32_t)(NDIRECT * BSIZE)) {
        printk("FS Error: file too large\n");
        return -1;
    }

    uint32_t total = 0;
    while (total < n) {
        uint32_t bn   = (off + total) / BSIZE;
        uint32_t boff = (off + total) % BSIZE;
        uint32_t blk  = bmap(ip, bn, 1);
        if (blk == 0) return -1;

        uint32_t chunk = BSIZE - boff;
        if (chunk > n - total) chunk = n - total;

        struct buf *b = bread(blk);
        memmove(b->data + boff, src + total, chunk);
        b->disk = 1;
        brelse(b);
        total += chunk;
    }

    if (off + n > ip->size) {
        ip->size = off + n;
        iupdate(ip);
    }
    return (int)total;
}

/* ------------------------------------------------------------------ */
/* Directory                                                           */
/* ------------------------------------------------------------------ */

struct inode *dirlookup(struct inode *dp, const char *name, uint32_t *poff) {
    iload(dp);
    if (dp->type != 1) return 0;   /* not a directory */

    struct dirent de;
    for (uint32_t off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
            break;
        if (de.inum == 0) continue;
        if (strncmp(de.name, name, DIRSIZ) == 0) {
            if (poff) *poff = off;
            return iget(de.inum);
        }
    }
    return 0;
}

int dirlink(struct inode *dp, const char *name, uint32_t inum) {
    iload(dp);

    /* check name doesn't already exist */
    struct inode *existing = dirlookup(dp, name, 0);
    if (existing) { iput(existing); return -1; }

    /* find a free slot */
    struct dirent de;
    uint32_t off;
    for (off = 0; off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
            break;
        if (de.inum == 0) break;   /* free entry */
    }

    memset(&de, 0, sizeof(de));
    de.inum = (uint16_t)inum;
    strncpy(de.name, name, DIRSIZ);
    if (writei(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
        return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Path resolution                                                     */
/* ------------------------------------------------------------------ */

/* Split the next path component from path into name[DIRSIZ+1].
   Returns pointer to rest of path, or NULL if no component left. */
static const char *skipelem(const char *path, char *name) {
    while (*path == '/') path++;
    if (*path == '\0') return 0;

    const char *s = path;
    while (*path != '/' && *path != '\0') path++;

    int len = path - s;
    if (len >= DIRSIZ) len = DIRSIZ;
    memmove(name, s, len);
    name[len] = '\0';
    return path;
}

struct inode *namei(const char *path) {
    struct inode *ip;

    /* Root inode is inum=1 */
    if (path[0] == '/') {
        ip = iget(1);
    } else {
        /* relative paths not supported yet — treat as absolute */
        ip = iget(1);
    }

    char name[DIRSIZ + 1];
    while ((path = skipelem(path, name)) != 0) {
        iload(ip);
        if (ip->type != 1) {     /* not a directory */
            iput(ip);
            return 0;
        }
        struct inode *next = dirlookup(ip, name, 0);
        iput(ip);
        if (next == 0) return 0;
        ip = next;
    }
    return ip;
}

/* ------------------------------------------------------------------ */
/* Self-test (kept for compatibility)                                  */
/* ------------------------------------------------------------------ */

void fs_test_self(void) {
    printk("[fs_test] starting inode layer test\n");

    /* allocate a file inode */
    struct inode *ip = ialloc(2);   /* type=2: regular file */
    if (ip == 0) { printk("[fs_test] ialloc failed\n"); return; }
    printk("[fs_test] allocated inode %d\n", ip->inum);

    /* write "Hello, FS!" into the file */
    char msg[] = "Hello, FS!";
    int w = writei(ip, msg, 0, sizeof(msg));
    printk("[fs_test] writei returned %d\n", w);

    /* read it back */
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int r = readi(ip, buf, 0, sizeof(msg));
    printk("[fs_test] readi returned %d, data=\"%s\"\n", r, buf);

    iput(ip);
    printk("[fs_test] done\n");
}
