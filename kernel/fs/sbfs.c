/*
 * sbfs.c — Simple Block Filesystem v1
 *
 * Provides read-write POSIX-like filesystem semantics on top of the buffer
 * cache and write-ahead log.  All mutating operations (write, create, unlink,
 * mkdir) must be surrounded by begin_op()/end_op().
 *
 * On-disk layout — see kernel/include/sbfs.h for constants and structs.
 */

#include <sbfs.h>
#include <bio.h>
#include <log.h>
#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <vfs.h>
#include <page_cache.h>
#include <printk.h>
#include <string.h>
#include <riscv.h>
#include <drivers/rtc.h>

/* -----------------------------------------------------------------------
 * Module-level state
 * ----------------------------------------------------------------------- */
static struct sb_superblock sb;   /* cached superblock                 */
static struct sbfs_inode icache[SBFS_ICACHE_MAX]; /* in-memory inodes  */
static int sbfs_ready = 0;        /* set to 1 after successful mount   */

/* -----------------------------------------------------------------------
 * IRQs-off "lock" — sufficient for single-hart
 * ----------------------------------------------------------------------- */
/* Save/restore SIE instead of unconditionally unmasking, so nested callers
 * (or kernel code already running with interrupts off) don't get preempted. */
static int fs_depth = 0;
static uint64_t fs_saved_sie = 0;
static inline void fs_lock(void) {
    uint64_t s = read_sstatus();
    write_sstatus(s & ~SSTATUS_SIE);
    if (fs_depth++ == 0) fs_saved_sie = s & SSTATUS_SIE;
}
static inline void fs_unlock(void) {
    if (--fs_depth == 0 && fs_saved_sie)
        write_sstatus(read_sstatus() | SSTATUS_SIE);
}

/* Wall-clock seconds since the Unix epoch, sourced from Goldfish RTC.
 * sbfs v1's on-disk inode has only one timestamp (mtime); we report
 * it as st_atime/st_mtime/st_ctime alike. Read access does not bump
 * mtime — equivalent to mounting Linux with "noatime", which is the
 * right tradeoff for a teaching kernel without writeback batching. */
static inline uint64_t sbfs_now(void) {
    return rtc_read_ns() / 1000000000ULL;
}

/* -----------------------------------------------------------------------
 * inode_ops forward declarations
 * ----------------------------------------------------------------------- */
static int  sbfs_op_read(struct inode *, uint64_t, void *, uint64_t);
static int  sbfs_readpage(struct inode *, uint64_t, void *);
static int  sbfs_writepage(struct inode *, uint64_t, const void *);
int         sbfs_writepage_locked(struct inode *, uint64_t, const void *);
static int  sbfs_op_write(struct inode *, uint64_t, const void *, uint64_t);
static int  sbfs_op_stat(struct inode *, struct stat *);
static int  sbfs_op_lookup(struct inode *, const char *, struct inode **);
static int  sbfs_op_getdents(struct inode *, uint64_t, void *, uint64_t, uint64_t *);
static int  sbfs_op_truncate(struct inode *);
static void sbfs_op_release(struct inode *);
static int  sbfs_op_create(struct inode *, const char *, struct inode **);
static int  sbfs_op_mkdir (struct inode *, const char *);
static int  sbfs_op_unlink(struct inode *, const char *);
static int  sbfs_op_link  (struct inode *, struct inode *, const char *);
static int  sbfs_op_rename(struct inode *, const char *,
                           struct inode *, const char *);

/* Internal helpers used before their definition. */
static void sbfs_itrunc(struct sbfs_inode *si);

static int  sbfs_op_symlink (struct inode *, const char *, const char *);
static int  sbfs_op_readlink(struct inode *, char *, uint64_t);

static const struct inode_ops sbfs_iops = {
    .read     = sbfs_op_read,
    .write    = sbfs_op_write,
    .stat     = sbfs_op_stat,
    .lookup   = sbfs_op_lookup,
    .getdents = sbfs_op_getdents,
    .truncate = sbfs_op_truncate,
    .release  = sbfs_op_release,
    .readlink = sbfs_op_readlink,
    .create   = sbfs_op_create,
    .mkdir    = sbfs_op_mkdir,
    .unlink   = sbfs_op_unlink,
    .link     = sbfs_op_link,
    .rename   = sbfs_op_rename,
    .symlink  = sbfs_op_symlink,
    .readpage  = sbfs_readpage,
    .writepage = sbfs_writepage,
    .writepage_locked = sbfs_writepage_locked,
};

/* -----------------------------------------------------------------------
 * Block allocation helpers
 * ----------------------------------------------------------------------- */

/* Allocate a free data block; returns its block number or 0 on ENOSPC. */
static uint32_t balloc(void) {
    struct buf *bp = bread(sb.bmapstart);
    for (uint32_t i = 0; i < sb.nblocks; i++) {
        int byte = i / 8, bit = i % 8;
        if (!(bp->data[byte] & (1u << bit))) {
            bp->data[byte] |= (1u << bit);
            log_write(bp);
            brelse(bp);
            /* Zero the newly allocated data block. */
            uint32_t dblock = sb.bmapstart + 1 + i;
            struct buf *nb = bread(dblock);
            memset(nb->data, 0, SBFS_BSIZE);
            log_write(nb);
            brelse(nb);
            return dblock;
        }
    }
    brelse(bp);
    return 0;   /* ENOSPC */
}

/* Free data block bno. */
static void bfree(uint32_t bno) {
    /* bno is an absolute block number; compute offset from DATA_START. */
    uint32_t data_start = sb.bmapstart + 1;
    if (bno < data_start || bno >= data_start + sb.nblocks) {
        printk("sbfs: bfree: bad block %u\n", bno);
        return;
    }
    uint32_t i = bno - data_start;
    struct buf *bp = bread(sb.bmapstart);
    bp->data[i / 8] &= ~(1u << (i % 8));
    log_write(bp);
    brelse(bp);
}

/* -----------------------------------------------------------------------
 * Inode cache
 * ----------------------------------------------------------------------- */

/* Read the on-disk inode into si->d if not already valid. */
static void sbfs_ilock(struct sbfs_inode *si) {
    if (si->valid) return;
    uint32_t block  = sb.inodestart + si->inum / 8;
    uint32_t offset = (si->inum % 8) * sizeof(struct sb_dinode);
    struct buf *bp  = bread(block);
    memcpy(&si->d, bp->data + offset, sizeof(struct sb_dinode));
    brelse(bp);
    si->valid = 1;
    /* Sync generic inode fields from dinode. type 3 was added for symlinks;
     * target string is stored in regular data blocks (same layout as files). */
    si->vnode.type = (si->d.type == 1) ? I_REG
                   : (si->d.type == 2) ? I_DIR
                   : (si->d.type == 3) ? I_LNK
                   : 0;
    si->vnode.size   = si->d.size;
    si->vnode.nlink  = si->d.nlink;
    si->vnode.mtime  = si->d.mtime;
}

/* Write si->d back to the inode block (must be inside a transaction). */
void sbfs_iupdate(struct sbfs_inode *si) {
    uint32_t block  = sb.inodestart + si->inum / 8;
    uint32_t offset = (si->inum % 8) * sizeof(struct sb_dinode);
    /* Keep dinode size/nlink in sync from the generic vnode. */
    si->d.size  = (uint32_t)si->vnode.size;
    si->d.nlink = (uint16_t)si->vnode.nlink;
    struct buf *bp = bread(block);
    memcpy(bp->data + offset, &si->d, sizeof(struct sb_dinode));
    log_write(bp);
    brelse(bp);
    si->dirty = 0;
}

/* Get (or create) an in-memory cache entry for inum.  Bumps refcnt. */
struct inode *sbfs_iget(uint32_t inum) {
    fs_lock();
    /* 1. Hit: already cached and live. */
    for (int i = 0; i < SBFS_ICACHE_MAX; i++) {
        if (icache[i].vnode.refcnt > 0 && icache[i].inum == inum) {
            icache[i].vnode.refcnt++;
            fs_unlock();
            return &icache[i].vnode;
        }
    }
    /* 2. Miss: find a free slot (refcnt == 0). */
    for (int i = 0; i < SBFS_ICACHE_MAX; i++) {
        if (icache[i].vnode.refcnt == 0) {
            icache[i].inum          = inum;
            icache[i].valid         = 0;
            icache[i].dirty         = 0;
            icache[i].vnode.refcnt  = 1;
            icache[i].vnode.ops     = &sbfs_iops;
            icache[i].vnode.fs_data = &icache[i];
            icache[i].vnode.mount_child = 0;
            icache[i].vnode.mount_parent = 0;
            fs_unlock();
            sbfs_ilock(&icache[i]);
            return &icache[i].vnode;
        }
    }
    fs_unlock();
    panic("sbfs: inode cache exhausted");
    return 0;
}

/* release callback — called by inode_put when refcnt → 0 */
static void sbfs_op_release(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;

    /* POSIX deferred-free: file was unlinked (nlink==0) while other refs
     * were still open.  Now that the last ref is gone, reclaim its blocks. */
    int needs_free = (si->valid && si->d.nlink == 0 && si->d.type != 0);

    if (needs_free || si->dirty) {
        begin_op();
        if (needs_free) {
            sbfs_itrunc(si);        /* frees data blocks + iupdate */
            si->d.type = 0;
            sbfs_iupdate(si);
        } else {
            sbfs_iupdate(si);
        }
        end_op();
    }
    si->dirty = 0;
    si->valid = 0;
    /* Flush page-cache entries keyed on this slot pointer BEFORE clearing
     * the inum so that a subsequent iget reusing the same slot pointer does
     * not pick up stale pages from the previous file occupant. */
    pcache_flush_inode(ip);
    si->inum  = 0;
    /* refcnt is already 0; slot is now free for reuse. */
}

/* -----------------------------------------------------------------------
 * Inode allocation
 * ----------------------------------------------------------------------- */
struct inode *sbfs_ialloc(uint16_t type) {
    for (uint32_t inum = 1; inum < sb.ninodes; inum++) {
        uint32_t block  = sb.inodestart + inum / 8;
        uint32_t offset = (inum % 8) * sizeof(struct sb_dinode);
        struct buf *bp  = bread(block);
        struct sb_dinode *d = (struct sb_dinode *)(bp->data + offset);
        if (d->type == 0) {
            /* Free inode — claim it. */
            memset(d, 0, sizeof(*d));
            d->type = type;
            d->mtime = sbfs_now();
            log_write(bp);
            brelse(bp);
            return sbfs_iget(inum);
        }
        brelse(bp);
    }
    return 0;   /* ENOSPC */
}

/* -----------------------------------------------------------------------
 * sbfs_readi — read up to n bytes from ip at offset off into buf
 * ----------------------------------------------------------------------- */
int sbfs_readi(struct inode *ip, uint64_t off, void *dst, uint64_t n) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    sbfs_ilock(si);

    if (off > si->d.size) return 0;
    if (off + n > si->d.size) n = si->d.size - off;
    if (n == 0) return 0;

    uint64_t total = 0;
    char *out = (char *)dst;
    while (total < n) {
        uint32_t bn   = (off + total) / SBFS_BSIZE;
        uint32_t boff = (off + total) % SBFS_BSIZE;

        uint32_t data_block;
        if (bn < SBFS_NDIR) {
            data_block = si->d.addrs[bn];
        } else {
            uint32_t rel = bn - SBFS_NDIR;
            uint32_t ii  = rel / SBFS_NBLK_PER_INDIR;
            uint32_t io  = rel % SBFS_NBLK_PER_INDIR;
            if (ii >= SBFS_NINDIR) break;
            uint32_t indir = si->d.addrs[SBFS_NDIR + ii];
            if (indir == 0) break;
            struct buf *ibp = bread(indir);
            data_block = ((uint32_t *)ibp->data)[io];
            brelse(ibp);
        }

        if (data_block == 0) break;
        struct buf *bp = bread(data_block);
        uint64_t chunk = SBFS_BSIZE - boff;
        if (chunk > n - total) chunk = n - total;
        memcpy(out + total, bp->data + boff, chunk);
        brelse(bp);
        total += chunk;
    }
    return (int)total;
}

/* -----------------------------------------------------------------------
 * sbfs_writei — write n bytes from src into ip at offset off
 *
 * Allocates data blocks as needed.  Refuses to exceed SBFS_MAX_FILE_SIZE.
 * Must be called inside begin_op/end_op.
 * Returns bytes written, or negative errno.
 * ----------------------------------------------------------------------- */
int sbfs_writei(struct inode *ip, uint64_t off, const void *src, uint64_t n) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    sbfs_ilock(si);

    if (off > SBFS_MAX_FILE_SIZE) return -EFBIG;
    if (n == 0) return 0;
    /* Clamp to cap. */
    if (off + n > SBFS_MAX_FILE_SIZE)
        n = SBFS_MAX_FILE_SIZE - off;

    uint64_t total = 0;
    const char *in = (const char *)src;
    int enospc = 0;
    while (total < n) {
        uint32_t bn   = (off + total) / SBFS_BSIZE;
        uint32_t boff = (off + total) % SBFS_BSIZE;

        uint32_t data_block;
        if (bn < SBFS_NDIR) {
            if (si->d.addrs[bn] == 0) {
                uint32_t nb = balloc();
                if (!nb) { enospc = 1; break; }
                si->d.addrs[bn] = nb;
                si->dirty = 1;
            }
            data_block = si->d.addrs[bn];
        } else {
            uint32_t rel = bn - SBFS_NDIR;
            uint32_t ii  = rel / SBFS_NBLK_PER_INDIR;
            uint32_t io  = rel % SBFS_NBLK_PER_INDIR;
            if (ii >= SBFS_NINDIR) break;

            if (si->d.addrs[SBFS_NDIR + ii] == 0) {
                uint32_t ib = balloc();
                if (!ib) { enospc = 1; break; }
                si->d.addrs[SBFS_NDIR + ii] = ib;
                si->dirty = 1;
            }

            struct buf *ibp = bread(si->d.addrs[SBFS_NDIR + ii]);
            uint32_t *ia = (uint32_t *)ibp->data;
            if (ia[io] == 0) {
                uint32_t nb = balloc();
                if (!nb) { brelse(ibp); enospc = 1; break; }
                ia[io] = nb;
                log_write(ibp);
            }
            data_block = ia[io];
            brelse(ibp);
        }

        struct buf *bp = bread(data_block);
        uint64_t chunk = SBFS_BSIZE - boff;
        if (chunk > n - total) chunk = n - total;
        memcpy(bp->data + boff, in + total, chunk);
        log_write(bp);
        brelse(bp);
        total += chunk;
    }

    if (off + total > si->d.size) {
        si->d.size = (uint32_t)(off + total);
        si->vnode.size = si->d.size;
        si->dirty = 1;
    }
    /* Bump mtime on any actual write (covers file content + dirent
     * mutations, since dirlink/dirunlink ride sbfs_writei). */
    if (total > 0) {
        si->d.mtime = sbfs_now();
        si->vnode.mtime = si->d.mtime;
        si->dirty = 1;
    }
    /* Always persist the inode before returning — this covers the partial-
     * write ENOSPC path where we've allocated blocks but not yet recorded
     * them in the on-disk inode. */
    if (si->dirty)
        sbfs_iupdate(si);

    if (total == 0 && enospc) return -ENOSPC;
    return (int)total;
}

/* -----------------------------------------------------------------------
 * sbfs_itrunc — free all data blocks of an inode
 * (must be inside a transaction)
 * ----------------------------------------------------------------------- */
static void sbfs_itrunc(struct sbfs_inode *si) {
    for (int bn = 0; bn < SBFS_NDIR; bn++) {
        if (si->d.addrs[bn]) {
            bfree(si->d.addrs[bn]);
            si->d.addrs[bn] = 0;
        }
    }
    for (int ii = 0; ii < SBFS_NINDIR; ii++) {
        if (si->d.addrs[SBFS_NDIR + ii]) {
            struct buf *ibp = bread(si->d.addrs[SBFS_NDIR + ii]);
            uint32_t *ia = (uint32_t *)ibp->data;
            for (int i = 0; i < SBFS_NBLK_PER_INDIR; i++) {
                if (ia[i]) bfree(ia[i]);
            }
            brelse(ibp);
            bfree(si->d.addrs[SBFS_NDIR + ii]);
            si->d.addrs[SBFS_NDIR + ii] = 0;
        }
    }
    si->d.size  = 0;
    si->vnode.size = 0;
    si->d.mtime = sbfs_now();
    si->vnode.mtime = si->d.mtime;
    si->dirty = 1;
    sbfs_iupdate(si);
}

/* -----------------------------------------------------------------------
 * Directory helpers
 * ----------------------------------------------------------------------- */

/* Look up name in dir; returns inode (refcnt bumped) or NULL. */
struct inode *sbfs_dirlookup(struct inode *dir, const char *name) {
    struct sbfs_inode *sd = (struct sbfs_inode *)dir;
    sbfs_ilock(sd);
    if (sd->d.type != 2) return 0;

    uint64_t sz = sd->d.size;
    for (uint64_t off = 0; off < sz; off += sizeof(struct sb_dirent)) {
        struct sb_dirent de;
        int r = sbfs_readi(dir, off, &de, sizeof(de));
        if (r != (int)sizeof(de)) break;
        if (de.inum == 0) continue;
        if (strncmp(de.name, name, SBFS_DIRSIZ) == 0)
            return sbfs_iget(de.inum);
    }
    return 0;
}

/* Add (name, inum) to dir.  Must be inside a transaction. */
int sbfs_dirlink(struct inode *dir, const char *name, uint32_t inum) {
    struct sbfs_inode *sd = (struct sbfs_inode *)dir;
    sbfs_ilock(sd);

    /* Search for a free slot (inum==0) or append. */
    uint64_t off;
    struct sb_dirent de;
    for (off = 0; off < sd->d.size; off += sizeof(de)) {
        int r = sbfs_readi(dir, off, &de, sizeof(de));
        if (r != (int)sizeof(de)) break;
        if (de.inum == 0) goto found;
    }
    /* No free slot — extend the directory. */
    if (sd->d.size + sizeof(de) > SBFS_MAX_FILE_SIZE) return -ENOSPC;
    off = sd->d.size;

found:
    memset(&de, 0, sizeof(de));
    de.inum = (uint16_t)inum;
    strncpy(de.name, name, SBFS_DIRSIZ);
    de.name[SBFS_DIRSIZ-1] = '\0';
    int r = sbfs_writei(dir, off, &de, sizeof(de));
    return (r == (int)sizeof(de)) ? 0 : -ENOSPC;
}

/* Remove entry named name from dir.  Must be inside a transaction. */
int sbfs_dirunlink(struct inode *dir, const char *name) {
    struct sbfs_inode *sd = (struct sbfs_inode *)dir;
    sbfs_ilock(sd);

    struct sb_dirent de;
    uint64_t off;
    for (off = 0; off < sd->d.size; off += sizeof(de)) {
        int r = sbfs_readi(dir, off, &de, sizeof(de));
        if (r != (int)sizeof(de)) break;
        if (de.inum != 0 && strncmp(de.name, name, SBFS_DIRSIZ) == 0) {
            memset(&de, 0, sizeof(de));
            return sbfs_writei(dir, off, &de, sizeof(de)) > 0 ? 0 : -EIO;
        }
    }
    return -ENOENT;
}

/* Check if a directory is empty (only "." and ".."). */
static int dir_is_empty(struct sbfs_inode *sd) {
    struct sb_dirent de;
    for (uint64_t off = 0; off < sd->d.size; off += sizeof(de)) {
        sbfs_readi(&sd->vnode, off, &de, sizeof(de));
        if (de.inum == 0) continue;
        if (strncmp(de.name, ".",  SBFS_DIRSIZ) == 0) continue;
        if (strncmp(de.name, "..", SBFS_DIRSIZ) == 0) continue;
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------
 * sbfs_create — allocate a new inode and link it into parent_dir
 * Must be called inside begin_op/end_op.
 * Returns the new inode (refcnt bumped) or NULL on error.
 * ----------------------------------------------------------------------- */
struct inode *sbfs_create(struct inode *parent, const char *name, uint16_t type) {
    /* Allocate inode. */
    struct inode *ip = sbfs_ialloc(type);
    if (!ip) return 0;
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    si->d.nlink = 1;
    si->vnode.nlink = 1;
    si->dirty = 1;

    if (type == 2) {
        /* Directory: add "." and ".." entries. */
        si->d.nlink = 2;   /* "." entry + parent's reference */
        si->vnode.nlink = 2;
        sbfs_dirlink(ip, ".",  si->inum);
        struct sbfs_inode *sp = (struct sbfs_inode *)parent;
        sbfs_dirlink(ip, "..", sp->inum);
    }

    sbfs_iupdate(si);

    /* Link into parent. */
    if (sbfs_dirlink(parent, name, si->inum) < 0) {
        /* Roll back: free any data blocks a new directory may have
         * allocated for its "." / ".." entries, then mark the inode free. */
        sbfs_itrunc(si);
        si->d.type  = 0;
        si->d.nlink = 0;
        si->vnode.nlink = 0;
        sbfs_iupdate(si);
        inode_put(ip);
        return 0;
    }

    if (type == 2) {
        /* A new subdirectory increments parent's nlink for the ".." entry. */
        struct sbfs_inode *sp = (struct sbfs_inode *)parent;
        sbfs_ilock(sp);
        sp->d.nlink++;
        sp->vnode.nlink++;
        sp->dirty = 1;
        sbfs_iupdate(sp);
    }

    return ip;
}

/* -----------------------------------------------------------------------
 * sbfs_unlink — remove name from parent; free inode if nlink → 0
 * Must be called inside begin_op/end_op.
 * ----------------------------------------------------------------------- */
int sbfs_unlink(struct inode *parent, const char *name) {
    /* Refuse to unlink "." and ".." */
    if (strncmp(name, ".",  SBFS_DIRSIZ) == 0 ||
        strncmp(name, "..", SBFS_DIRSIZ) == 0)
        return -EINVAL;

    struct inode *ip = sbfs_dirlookup(parent, name);
    if (!ip) return -ENOENT;
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    sbfs_ilock(si);

    if (si->d.type == 2 && !dir_is_empty(si)) {
        inode_put(ip);
        return -ENOTEMPTY;
    }

    /* Remove directory entry. */
    sbfs_dirunlink(parent, name);

    if (si->d.type == 2) {
        /* Subdirectory: decrement parent nlink for the ".." back-ref. */
        struct sbfs_inode *sp = (struct sbfs_inode *)parent;
        sp->d.nlink--;
        sp->vnode.nlink--;
        sp->dirty = 1;
        sbfs_iupdate(sp);
    }

    si->d.nlink--;
    si->vnode.nlink--;
    si->d.mtime = sbfs_now();
    si->vnode.mtime = si->d.mtime;
    si->dirty = 1;
    sbfs_iupdate(si);

    if (si->d.nlink == 0 && si->vnode.refcnt == 1) {
        /* No open fds and no more links — free blocks. */
        sbfs_itrunc(si);
        si->d.type = 0;
        sbfs_iupdate(si);
    }

    inode_put(ip);
    return 0;
}

/* -----------------------------------------------------------------------
 * inode_ops implementations
 * ----------------------------------------------------------------------- */

static int sbfs_readpage(struct inode *ip, uint64_t pgidx, void *page) {
    uint64_t off = pgidx * 4096UL;
    int n = sbfs_readi(ip, off, page, 4096);
    if (n < 0) return n;
    if (n < 4096) memset((char *)page + n, 0, 4096 - n);
    return 0;
}

/* Caller MUST be inside begin_op/end_op. */
static int sbfs_writepage(struct inode *ip, uint64_t pgidx, const void *page) {
    uint64_t off = pgidx * 4096UL;
    uint64_t end = off + 4096UL;
    if (end > ip->size) end = ip->size;
    if (end <= off) return 0;
    int n = sbfs_writei(ip, off, page, end - off);
    if (n < 0) return n;
    return 0;
}

/* Public wrapper: takes the begin_op/end_op transaction itself.
 * Used by page-cache eviction and msync/munmap flush paths. */
int sbfs_writepage_locked(struct inode *ip, uint64_t pgidx, const void *page) {
    begin_op();
    int rc = sbfs_writepage(ip, pgidx, page);
    end_op();
    return rc;
}

static int sbfs_op_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    return generic_file_read(ip, off, buf, n);
}

static int sbfs_op_write(struct inode *ip, uint64_t off, const void *buf, uint64_t n) {
    int ret;
    begin_op();
    ret = sbfs_writei(ip, off, buf, n);
    end_op();
    if (ret > 0)
        pcache_invalidate_range(ip, off, (uint64_t)ret);
    return ret;
}

static int sbfs_op_truncate(struct inode *ip) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    sbfs_ilock(si);
    if (si->d.type != 1) return -EINVAL;   /* regular files only */
    begin_op();
    sbfs_itrunc(si);
    end_op();
    pcache_invalidate_range(ip, 0, ~0ULL);
    return 0;
}

static int sbfs_op_stat(struct inode *ip, struct stat *st) {
    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    sbfs_ilock(si);
    st->st_ino   = si->inum;
    st->st_nlink = si->d.nlink;
    st->st_size  = si->d.size;
    if (si->d.type == 1) {
        st->st_mode = 0100644;   /* regular file */
    } else if (si->d.type == 2) {
        st->st_mode = 040755;    /* directory    */
    } else if (si->d.type == 3) {
        st->st_mode = 0120777;   /* S_IFLNK | 0777 */
    } else {
        st->st_mode = 0;
    }
    /* sbfs v1 has a single on-disk timestamp; report it as all three
     * stat fields. Documented deviation from POSIX. */
    st->st_atime = si->d.mtime;
    st->st_mtime = si->d.mtime;
    st->st_ctime = si->d.mtime;
    return 0;
}

static int sbfs_op_lookup(struct inode *dir, const char *name, struct inode **out) {
    /* Handle "." */
    if (strncmp(name, ".", SBFS_DIRSIZ) == 0) {
        *out = inode_get(dir);
        return 0;
    }
    /* Handle ".." */
    if (strncmp(name, "..", SBFS_DIRSIZ) == 0) {
        struct inode *parent = sbfs_dirlookup(dir, "..");
        if (!parent) return -ENOENT;
        *out = parent;
        return 0;
    }
    struct inode *ip = sbfs_dirlookup(dir, name);
    if (!ip) return -ENOENT;
    *out = ip;
    return 0;
}

static int sbfs_op_getdents(struct inode *dir, uint64_t off, void *buf,
                             uint64_t bufsize, uint64_t *out_next) {
    struct sbfs_inode *sd = (struct sbfs_inode *)dir;
    sbfs_ilock(sd);

    int written = 0;
    char *dst = (char *)buf;

    while (off < sd->d.size) {
        struct sb_dirent de;
        int r = sbfs_readi(dir, off, &de, sizeof(de));
        if (r != (int)sizeof(de)) break;
        off += sizeof(de);
        if (de.inum == 0) continue;

        /* Build a dirent64 record. */
        int namelen = 0;
        while (namelen < SBFS_DIRSIZ && de.name[namelen]) namelen++;
        int reclen = DIRENT64_FIXED_LEN + namelen + 1;
        reclen = (reclen + 7) & ~7;   /* 8-byte aligned */

        if (written + reclen > (int)bufsize) break;

        struct dirent64 *d64 = (struct dirent64 *)(dst + written);
        d64->d_ino    = de.inum;
        d64->d_off    = off;
        d64->d_reclen = (uint16_t)reclen;
        /* Determine type. */
        struct inode *tip = sbfs_iget(de.inum);
        d64->d_type = (tip && tip->type == I_DIR) ? DT_DIR :
                      (tip && tip->type == I_REG) ? DT_REG :
                      (tip && tip->type == I_LNK) ? DT_LNK : DT_UNKNOWN;
        if (tip) inode_put(tip);
        memcpy(d64->d_name, de.name, namelen);
        d64->d_name[namelen] = '\0';
        written += reclen;
    }

    *out_next = off;
    return written;
}

static int sbfs_op_create(struct inode *parent, const char *name,
                          struct inode **out) {
    begin_op();
    struct inode *ip = sbfs_create(parent, name, 1 /* regular file */);
    end_op();
    if (!ip) return -ENOSPC;
    *out = ip;
    return 0;
}

static int sbfs_op_mkdir(struct inode *parent, const char *name) {
    begin_op();
    struct inode *ip = sbfs_create(parent, name, 2 /* directory */);
    end_op();
    if (!ip) return -ENOSPC;
    inode_put(ip);
    return 0;
}

static int sbfs_op_unlink(struct inode *parent, const char *name) {
    begin_op();
    int rc = sbfs_unlink(parent, name);
    end_op();
    return rc;
}

/* -----------------------------------------------------------------------
 * sbfs_link — add a directory entry pointing to an existing inode.
 *
 * Caller (sys_link) has already verified:
 *   - target is not a directory  (POSIX: hard link to dir → -EPERM)
 *   - target is in the same fs as parent  (-EXDEV otherwise)
 *   - `name` does not already exist in parent  (-EEXIST otherwise)
 *
 * On success the target's nlink is incremented and the in-memory +
 * on-disk inode are updated.
 * ----------------------------------------------------------------------- */
static int sbfs_link(struct inode *parent, struct inode *target, const char *name) {
    struct sbfs_inode *si = (struct sbfs_inode *)target;

    int rc = sbfs_dirlink(parent, name, si->inum);
    if (rc < 0) return rc;

    si->d.nlink++;
    si->vnode.nlink++;
    si->d.mtime = sbfs_now();
    si->vnode.mtime = si->d.mtime;
    si->dirty = 1;
    sbfs_iupdate(si);
    return 0;
}

static int sbfs_op_link(struct inode *parent, struct inode *target, const char *name) {
    begin_op();
    int rc = sbfs_link(parent, target, name);
    end_op();
    return rc;
}

/* -----------------------------------------------------------------------
 * sbfs_op_symlink — create a symbolic link.
 *
 * On-disk representation: a new inode of type 3 (I_LNK) with the target
 * string stored in regular data blocks via sbfs_writei. No new on-disk
 * structures or format bumps — type==3 was a previously-unused value
 * in the existing 16-bit dinode.type field.
 * ----------------------------------------------------------------------- */
static int sbfs_op_symlink(struct inode *parent, const char *name,
                           const char *target) {
    if (!target || target[0] == '\0') return -EINVAL;

    /* Bound target length. sbfs files cap at a few KiB anyway; cap at
     * 1023 bytes (PATH_MAX is 4096 but this matches what readlink users
     * tend to pass for buf size and keeps it small). */
    uint64_t tlen = 0;
    while (target[tlen] && tlen < 1024) tlen++;
    if (tlen >= 1024) return -ENAMETOOLONG;

    begin_op();
    struct inode *ip = sbfs_create(parent, name, 3 /* SBFS_T_LNK */);
    if (!ip) { end_op(); return -ENOSPC; }

    int w = sbfs_writei(ip, 0, target, tlen);
    if (w != (int)tlen) {
        /* Roll back: drop the link from parent and mark the inode free. */
        sbfs_unlink(parent, name);
        end_op();
        inode_put(ip);
        return -ENOSPC;
    }

    struct sbfs_inode *si = (struct sbfs_inode *)ip;
    si->d.size = (uint32_t)tlen;
    si->vnode.size = tlen;
    si->dirty = 1;
    sbfs_iupdate(si);
    end_op();
    inode_put(ip);
    return 0;
}

static int sbfs_op_readlink(struct inode *ip, char *buf, uint64_t n) {
    if (ip->type != I_LNK) return -EINVAL;
    int got = sbfs_readi(ip, 0, buf, n);
    if (got < 0) return got;
    return got;
}

/* -----------------------------------------------------------------------
 * sbfs_rename — atomically move (old_p, old_name) to (new_p, new_name).
 *
 * Caller (sys_rename) has already verified that both parents are sbfs
 * directories (same filesystem). This function handles all the messy
 * POSIX semantics:
 *   - same-path no-op
 *   - replacing an existing newpath (file→file or empty-dir→empty-dir)
 *   - rejecting cross-type replacement (-EISDIR / -ENOTDIR)
 *   - rejecting non-empty target (-ENOTEMPTY)
 *   - cross-parent directory move: fix ".." and adjust nlinks on both parents
 *   - loop prevention: cannot rename a directory into its own subtree
 *
 * Must be called inside begin_op/end_op.
 * ----------------------------------------------------------------------- */
static int sbfs_rename(struct inode *old_p, const char *old_name,
                       struct inode *new_p, const char *new_name) {
    /* 1. Source must exist. */
    struct inode *src = sbfs_dirlookup(old_p, old_name);
    if (!src) return -ENOENT;

    /* 2. Same parent + same name = POSIX no-op. */
    if (old_p == new_p && strncmp(old_name, new_name, SBFS_DIRSIZ) == 0) {
        inode_put(src);
        return 0;
    }

    /* 3. Look up dst (may or may not exist). */
    struct inode *dst = sbfs_dirlookup(new_p, new_name);

    /* 4. Loop prevention: if src is a directory, new_p must not be src
     *    or any descendant of src. */
    if (src->type == I_DIR && old_p != new_p) {
        if (new_p == src) {
            if (dst) inode_put(dst);
            inode_put(src);
            return -EINVAL;
        }
        struct inode *walk = sbfs_dirlookup(new_p, "..");
        while (walk) {
            if (walk == src) {
                inode_put(walk);
                if (dst) inode_put(dst);
                inode_put(src);
                return -EINVAL;
            }
            struct inode *parent = sbfs_dirlookup(walk, "..");
            if (parent == walk) {
                /* root: ".." points to itself — done walking. */
                inode_put(parent);
                inode_put(walk);
                break;
            }
            inode_put(walk);
            walk = parent;
        }
    }

    /* 5. If dst exists, validate replace semantics and remove it. */
    if (dst) {
        if (src->type == I_DIR && dst->type != I_DIR) {
            inode_put(dst); inode_put(src);
            return -ENOTDIR;
        }
        if (src->type != I_DIR && dst->type == I_DIR) {
            inode_put(dst); inode_put(src);
            return -EISDIR;
        }
        if (dst->type == I_DIR &&
            !dir_is_empty((struct sbfs_inode *)dst)) {
            inode_put(dst); inode_put(src);
            return -ENOTEMPTY;
        }
        /* Release our ref before sbfs_unlink (which takes its own ref). */
        inode_put(dst);
        int rc = sbfs_unlink(new_p, new_name);
        if (rc < 0) {
            inode_put(src);
            return rc;
        }
    }

    /* 6. Add new dirent, then remove old. */
    struct sbfs_inode *src_si = (struct sbfs_inode *)src;
    int rc = sbfs_dirlink(new_p, new_name, src_si->inum);
    if (rc < 0) {
        inode_put(src);
        return rc;
    }
    rc = sbfs_dirunlink(old_p, old_name);
    if (rc < 0) {
        int rollback_rc = sbfs_dirunlink(new_p, new_name);
        if (rollback_rc < 0) {
            printk("sbfs: rename rollback failed (%d) after unlink error %d\n",
                   rollback_rc, rc);
        }
        inode_put(src);
        return rc;
    }

    /* 7. Cross-parent directory move: fix src's ".." and adjust nlinks.
     *
     * Order matters here: update ".." first and check both dir ops, then
     * touch parent nlinks only on success. If we adjusted nlinks first
     * and ".." rewriting failed (e.g. ENOSPC/EIO during dirlink), the
     * filesystem would be permanently inconsistent — moved dir with no
     * (or stale) ".." entry plus mis-counted parent nlinks.
     */
    if (src->type == I_DIR && old_p != new_p) {
        struct sbfs_inode *new_p_si = (struct sbfs_inode *)new_p;
        struct sbfs_inode *old_p_si = (struct sbfs_inode *)old_p;

        int dr = sbfs_dirunlink(src, "..");
        if (dr < 0) {
            /* Every directory must have ".." — this should be unreachable,
             * but propagate rather than silently corrupting nlink. */
            inode_put(src);
            return dr;
        }
        int dl = sbfs_dirlink(src, "..", new_p_si->inum);
        if (dl < 0) {
            /* Best-effort recovery: put the old ".." back so the dir is
             * not left with no parent reference at all. */
            (void)sbfs_dirlink(src, "..", old_p_si->inum);
            inode_put(src);
            return dl;
        }

        /* ".." update committed — now safe to adjust parent nlinks. */
        uint64_t now = sbfs_now();

        old_p_si->d.nlink--;
        old_p_si->vnode.nlink--;
        old_p_si->d.mtime = now;
        old_p_si->vnode.mtime = now;
        old_p_si->dirty = 1;
        sbfs_iupdate(old_p_si);

        new_p_si->d.nlink++;
        new_p_si->vnode.nlink++;
        new_p_si->d.mtime = now;
        new_p_si->vnode.mtime = now;
        new_p_si->dirty = 1;
        sbfs_iupdate(new_p_si);
    }

    inode_put(src);
    return 0;
}

static int sbfs_op_rename(struct inode *old_p, const char *old_name,
                          struct inode *new_p, const char *new_name) {
    begin_op();
    int rc = sbfs_rename(old_p, old_name, new_p, new_name);
    end_op();
    return rc;
}

/* -----------------------------------------------------------------------
 * sbfs_init — read superblock, replay log. Does NOT register a mount.
 * Use sbfs_attach(target) after init to make sbfs visible at a path.
 * ----------------------------------------------------------------------- */
int sbfs_init(void) {
    /* Read superblock. */
    struct buf *bp = bread(1);   /* block 1 = superblock */
    memcpy(&sb, bp->data, sizeof(sb));
    brelse(bp);

    if (sb.magic != SBFS_MAGIC) {
        printk("sbfs: bad magic 0x%x (want 0x%x)\n", sb.magic, SBFS_MAGIC);
        return -EINVAL;
    }

    printk("sbfs: superblock ok (size=%u nblocks=%u ninodes=%u logstart=%u)\n",
           sb.size, sb.nblocks, sb.ninodes, sb.logstart);

    /* Initialise log and replay any crashed transaction. The disk size
     * bounds the set of blocks log replay is allowed to touch. */
    log_init(sb.logstart, sb.nlog, sb.size);
    recover_from_log();

    sbfs_ready = 1;
    return 0;
}

/* Register sbfs root at `target`. Must be called after sbfs_init().
 * Idempotent via mount_fs same-root detection (Task 3). */
int sbfs_attach(const char *target) {
    if (!sbfs_ready) return -ENODEV;
    struct inode *root = sbfs_iget(SBFS_ROOTINUM);
    if (!root) return -ENOMEM;
    int rc = mount_fs(target, root);
    if (rc < 0) {
        inode_put(root);
        return rc;
    }
    printk("sbfs: mounted %s\n", target);
    return 0;
}
