/*
 * tmpfs.c — in-memory filesystem for /tmp.
 *
 * Storage:
 *   - Static array of TMPFS_NINODES inodes; each is either free (in_use==0)
 *     or holds a regular file or directory.
 *   - File data: fixed array of page pointers (file_pages[]) indexed by
 *     page number. Each slot is NULL (sparse hole) or a 4 KiB page from
 *     page_alloc().
 *   - Directory entries: linked list of struct tmpfs_dirent records,
 *     each occupying its own 4 KiB page.
 *
 * No on-disk format, no WAL. Crash safety is trivial — RAM is gone after
 * a crash anyway. All mutating ops are atomic w.r.t. interrupts because
 * a single fs_lock() / fs_unlock() bracket disables S-mode interrupts on
 * this single-hart kernel (same pattern as sbfs).
 *
 * mtime is sourced from Goldfish RTC (real wall-clock). atime/ctime are
 * reported equal to mtime — same single-timestamp convention as sbfs v1.
 */

#include <tmpfs.h>
#include <inode.h>
#include <stat.h>
#include <vfs.h>
#include <pmem.h>
#include <printk.h>
#include <string.h>
#include <errno.h>
#include <riscv.h>
#include <drivers/rtc.h>

/* ------------------------------------------------------------------ */
/* Internal types                                                     */
/* ------------------------------------------------------------------ */

#define TMPFS_PAGE_SIZE       4096
#define TMPFS_PAGES_PER_FILE  (TMPFS_MAX_FILESIZE / TMPFS_PAGE_SIZE)  /* 64 */

struct tmpfs_dirent {
    struct tmpfs_dirent  *next;
    struct tmpfs_inode   *target;        /* NULL after unlink */
    char                  name[TMPFS_DIRSIZ];
};

struct tmpfs_inode {
    struct inode         vnode;          /* MUST be first */
    int                  in_use;         /* 1 = allocated, 0 = free slot */
    /* Type-discriminated by vnode.type (I_REG / I_DIR).
     *
     * For I_REG: file_pages[i] either NULL (sparse hole) or a
     *   page_alloc()'d 4 KiB page. Direct array indexing — no struct
     *   embedding tricks, no risk of writing past the allocated page.
     *
     * For I_DIR: dirents is a linked list of child entries. Each
     *   tmpfs_dirent is small (44 bytes) and gets its own 4 KiB page
     *   (wasteful but bounded by TMPFS_NINODES). parent points at the
     *   containing dir so lookup(".." ) and rename loop checks work
     *   without per-dir ".." dirents. NULL on the root inode. */
    void                *file_pages[TMPFS_PAGES_PER_FILE];
    struct tmpfs_dirent *dirents;
    struct tmpfs_inode  *parent;
};

/* ------------------------------------------------------------------ */
/* Module state                                                       */
/* ------------------------------------------------------------------ */

static struct tmpfs_inode inodes[TMPFS_NINODES];
/* Static dirent pool. Each inode owns at least one dirent at create time;
 * hardlinks (tmpfs_op_link) consume an extra slot per additional name.
 * Sizing at TMPFS_NINODES is enough for the common case of one name per
 * inode and returns -ENOSPC gracefully if a link-heavy workload exhausts
 * the pool. Replaces the previous one-page-per-dirent allocation (T3.25). */
static struct tmpfs_dirent dirent_pool[TMPFS_NINODES];
static int                tmpfs_ready = 0;

static struct tmpfs_dirent *tmpfs_dirent_alloc(void) {
    for (int i = 0; i < TMPFS_NINODES; i++) {
        if (!dirent_pool[i].target) {
            memset(&dirent_pool[i], 0, sizeof(dirent_pool[i]));
            return &dirent_pool[i];
        }
    }
    return 0;
}

static void tmpfs_dirent_free(struct tmpfs_dirent *d) {
    /* Mark free via target=NULL; the pool sweep above relies on it. */
    d->target = 0;
    d->next   = 0;
    d->name[0] = 0;
}

/* IRQ-off lock — same pattern as sbfs. */
static int      fs_depth = 0;
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

static inline uint64_t tmpfs_now(void) {
    return realtime_ns() / 1000000000ULL;
}

/* ------------------------------------------------------------------ */
/* Forward declarations + vtable                                      */
/* ------------------------------------------------------------------ */

static int  tmpfs_op_read    (struct inode *, uint64_t, void *, uint64_t);
static int  tmpfs_op_write   (struct inode *, uint64_t, const void *, uint64_t);
static int  tmpfs_op_stat    (struct inode *, struct stat *);
static int  tmpfs_op_lookup  (struct inode *, const char *, struct inode **);
static int  tmpfs_op_getdents(struct inode *, uint64_t, void *, uint64_t,
                              uint64_t *);
static int  tmpfs_op_truncate(struct inode *);
static void tmpfs_op_release (struct inode *);
static int  tmpfs_op_create  (struct inode *, const char *, struct inode **);
static int  tmpfs_op_mkdir   (struct inode *, const char *);
static int  tmpfs_op_unlink  (struct inode *, const char *);
static int  tmpfs_op_link    (struct inode *, struct inode *, const char *);
static int  tmpfs_op_rename  (struct inode *, const char *,
                              struct inode *, const char *);
static int  tmpfs_op_symlink (struct inode *, const char *, const char *);
static int  tmpfs_op_readlink(struct inode *, char *, uint64_t);

static const struct inode_ops tmpfs_iops = {
    .read     = tmpfs_op_read,
    .write    = tmpfs_op_write,
    .stat     = tmpfs_op_stat,
    .lookup   = tmpfs_op_lookup,
    .getdents = tmpfs_op_getdents,
    .truncate = tmpfs_op_truncate,
    .release  = tmpfs_op_release,
    .readlink = tmpfs_op_readlink,
    .create   = tmpfs_op_create,
    .mkdir    = tmpfs_op_mkdir,
    .unlink   = tmpfs_op_unlink,
    .link     = tmpfs_op_link,
    .rename   = tmpfs_op_rename,
    .symlink  = tmpfs_op_symlink,
};

/* ------------------------------------------------------------------ */
/* Inode pool                                                         */
/* ------------------------------------------------------------------ */

/* Allocate a free inode slot. Returns NULL when the pool is full. */
static struct tmpfs_inode *tmpfs_ialloc(int type) {
    /* slot 0 is the root inode — never reallocate it. */
    for (int i = 1; i < TMPFS_NINODES; i++) {
        if (!inodes[i].in_use) {
            struct tmpfs_inode *ti = &inodes[i];
            memset(ti, 0, sizeof(*ti));
            ti->in_use         = 1;
            ti->vnode.type     = type;
            ti->vnode.mode     = (type == I_DIR) ? (S_IFDIR | 0777) :
                                 (type == I_LNK) ? (S_IFLNK | 0777) :
                                                   (S_IFREG | 0666);
            ti->vnode.refcnt   = 1;
            ti->vnode.nlink    = 1;
            ti->vnode.size     = 0;
            ti->vnode.mtime    = tmpfs_now();
            ti->vnode.ops      = &tmpfs_iops;
            ti->vnode.fs_data  = ti;
            return ti;
        }
    }
    return 0;
}

/* Free an inode's storage (pages or dirents) and mark slot free.
 * Caller has confirmed nlink == 0 && refcnt == 0. */
static void tmpfs_ifree(struct tmpfs_inode *ti) {
    if (ti->vnode.type == I_REG || ti->vnode.type == I_LNK) {
        /* Symlinks reuse file_pages[0] for the target string; the loop
         * below covers both REG and LNK because all other slots are NULL
         * on a symlink. */
        for (int i = 0; i < TMPFS_PAGES_PER_FILE; i++) {
            if (ti->file_pages[i]) {
                page_free(ti->file_pages[i]);
                ti->file_pages[i] = 0;
            }
        }
    } else if (ti->vnode.type == I_DIR) {
        struct tmpfs_dirent *d = ti->dirents;
        while (d) {
            struct tmpfs_dirent *next = d->next;
            tmpfs_dirent_free(d);
            d = next;
        }
        ti->dirents = 0;
    }
    ti->in_use     = 0;
    ti->vnode.type = 0;
}

/* ------------------------------------------------------------------ */
/* File data: page-array helpers                                      */
/* ------------------------------------------------------------------ */

/* Return the page covering byte offset `off`, allocating it if absent.
 * Returns NULL on out-of-range or ENOSPC. */
static void *tmpfs_get_page(struct tmpfs_inode *ti, uint64_t off) {
    uint64_t idx = off / TMPFS_PAGE_SIZE;
    if (idx >= TMPFS_PAGES_PER_FILE) return 0;
    if (!ti->file_pages[idx]) {
        void *raw = page_alloc();
        if (!raw) return 0;
        memset(raw, 0, TMPFS_PAGE_SIZE);
        ti->file_pages[idx] = raw;
    }
    return ti->file_pages[idx];
}

/* Return the page covering `off`, or NULL if absent (sparse hole or oob). */
static void *tmpfs_find_page(struct tmpfs_inode *ti, uint64_t off) {
    uint64_t idx = off / TMPFS_PAGE_SIZE;
    if (idx >= TMPFS_PAGES_PER_FILE) return 0;
    return ti->file_pages[idx];
}

/* ------------------------------------------------------------------ */
/* Directory: dirent helpers                                          */
/* ------------------------------------------------------------------ */

/* Reject names that would not fit in TMPFS_DIRSIZ (including NUL).
 * Without this, tmpfs_dirlink silently truncates and two distinct
 * overlong names with the same prefix would alias, breaking lookup
 * and rename's same-path check. Callers translate to -ENAMETOOLONG. */
static int tmpfs_name_ok(const char *name) {
    int i = 0;
    while (name[i] && i < TMPFS_DIRSIZ) i++;
    return (i < TMPFS_DIRSIZ);
}

static int name_eq(const char *a, const char *b) {
    for (int i = 0; i < TMPFS_DIRSIZ; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == 0)    return 1;
    }
    return 1;
}

static struct tmpfs_dirent *tmpfs_dirfind(struct tmpfs_inode *dir, const char *name) {
    for (struct tmpfs_dirent *d = dir->dirents; d; d = d->next) {
        if (d->target && name_eq(d->name, name)) return d;
    }
    return 0;
}

/* Add (name, target) to dir. Returns 0 or -ENOSPC.
 * Assumes caller has already verified name doesn't exist. */
static int tmpfs_dirlink(struct tmpfs_inode *dir, const char *name,
                         struct tmpfs_inode *target) {
    struct tmpfs_dirent *de = tmpfs_dirent_alloc();
    if (!de) return -ENOSPC;
    de->target = target;
    int i = 0;
    while (i < TMPFS_DIRSIZ - 1 && name[i]) { de->name[i] = name[i]; i++; }
    de->name[i] = 0;
    de->next = dir->dirents;
    dir->dirents = de;
    dir->vnode.size += sizeof(*de);   /* hint for stat */
    return 0;
}

/* Remove the dirent named `name` from dir. Returns 0 or -ENOENT. */
static int tmpfs_dirunlink(struct tmpfs_inode *dir, const char *name) {
    struct tmpfs_dirent **slot = &dir->dirents;
    while (*slot) {
        struct tmpfs_dirent *d = *slot;
        if (d->target && name_eq(d->name, name)) {
            *slot = d->next;
            uint64_t sz = sizeof(*d);
            tmpfs_dirent_free(d);
            if (dir->vnode.size >= sz)
                dir->vnode.size -= sz;
            return 0;
        }
        slot = &d->next;
    }
    return -ENOENT;
}

/* Is this dir empty (no entries except . and ..)? */
static int tmpfs_dir_is_empty(struct tmpfs_inode *dir) {
    for (struct tmpfs_dirent *d = dir->dirents; d; d = d->next) {
        if (!d->target) continue;
        if (d->name[0] == '.' && d->name[1] == 0) continue;
        if (d->name[0] == '.' && d->name[1] == '.' && d->name[2] == 0) continue;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* inode_ops implementations                                          */
/* ------------------------------------------------------------------ */

static int tmpfs_op_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    if (ti->vnode.type != I_REG) return -EISDIR;

    /* Read size + clamp under fs_lock: a concurrent write/truncate
     * (timer-preempted scheduling) could otherwise shrink size between
     * the bound check and the read, allowing reads past current EOF.
     * Overflow-safe form: `n > size - off` after off <= size. */
    fs_lock();
    if (off >= ti->vnode.size) { fs_unlock(); return 0; }
    if (n > ti->vnode.size - off) n = ti->vnode.size - off;
    uint64_t total = 0;
    while (total < n) {
        void *pg = tmpfs_find_page(ti, off + total);
        uint64_t poff = (off + total) % TMPFS_PAGE_SIZE;
        uint64_t chunk = TMPFS_PAGE_SIZE - poff;
        if (chunk > n - total) chunk = n - total;
        if (pg) memcpy((char *)buf + total, (char *)pg + poff, chunk);
        else    memset((char *)buf + total, 0, chunk);   /* sparse hole */
        total += chunk;
    }
    fs_unlock();
    return (int)total;
}

static int tmpfs_op_write(struct inode *ip, uint64_t off, const void *buf, uint64_t n) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    if (ti->vnode.type != I_REG) return -EISDIR;
    /* A non-zero-length write that starts at or past the file-size cap
     * cannot make any progress. Returning 0 in that case (n clamps to
     * 0, then the n==0 short-circuit fires) makes user code spin. So
     * surface -EFBIG explicitly. Zero-length writes still succeed as
     * a no-op regardless of offset. */
    if (n == 0) return 0;
    if (off >= TMPFS_MAX_FILESIZE) return -EFBIG;
    /* Overflow-safe clamp; mirrors the read path. Partial writes that
     * straddle the cap return the clamped count, not -EFBIG. */
    if (n > (uint64_t)TMPFS_MAX_FILESIZE - off)
        n = (uint64_t)TMPFS_MAX_FILESIZE - off;

    fs_lock();
    uint64_t total = 0;
    int err = 0;
    while (total < n) {
        void *pg = tmpfs_get_page(ti, off + total);
        if (!pg) { err = -ENOSPC; break; }
        uint64_t poff = (off + total) % TMPFS_PAGE_SIZE;
        uint64_t chunk = TMPFS_PAGE_SIZE - poff;
        if (chunk > n - total) chunk = n - total;
        memcpy((char *)pg + poff, (const char *)buf + total, chunk);
        total += chunk;
    }
    if (off + total > ti->vnode.size) ti->vnode.size = off + total;
    if (total > 0) ti->vnode.mtime = tmpfs_now();
    fs_unlock();
    return total ? (int)total : err;
}

static int tmpfs_op_stat(struct inode *ip, struct stat *st) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    /* fs_lock so size/nlink/mtime/mode are read atomically with respect
     * to concurrent mutators (write, truncate, link, unlink, rename). */
    fs_lock();
    st->st_dev   = 5;                     /* arbitrary distinct id */
    st->st_ino   = (uint64_t)(uintptr_t)ti;
    st->st_mode  = ti->vnode.mode;
    st->st_nlink = ti->vnode.nlink;
    st->st_uid   = ti->vnode.uid;
    st->st_gid   = ti->vnode.gid;
    st->st_size  = ti->vnode.size;
    STAT_SET_TIMES(st, ti->vnode.mtime);
    /* tmpfs storage is page-based: report the page size as the preferred
     * I/O size, and count actually-allocated pages (not ceil(size/512))
     * so sparse holes don't inflate st_blocks. Each st_block is 512 B. */
    st->st_blksize = TMPFS_PAGE_SIZE;
    uint64_t npages = 0;
    if (ti->vnode.type == I_REG) {
        for (int i = 0; i < TMPFS_PAGES_PER_FILE; i++) {
            if (ti->file_pages[i]) npages++;
        }
    } else if (ti->vnode.type == I_DIR) {
        /* tmpfs_dirlink allocates one full page per dirent. */
        for (struct tmpfs_dirent *d = ti->dirents; d; d = d->next) npages++;
    }
    st->st_blocks = npages * (TMPFS_PAGE_SIZE / 512);
    fs_unlock();
    return 0;
}

static int tmpfs_op_lookup(struct inode *dir, const char *name,
                           struct inode **out) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)dir;
    if (td->vnode.type != I_DIR) return -ENOTDIR;

    if (name[0] == '.' && name[1] == 0) {
        *out = inode_get(dir);
        return 0;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        /* Mount-root ".." is intercepted by namei via mount_parent
         * before reaching here. For nested dirs return the tracked
         * parent; for the unmounted root (defensive) stay at root. */
        struct tmpfs_inode *par = td->parent ? td->parent : td;
        *out = inode_get(&par->vnode);
        return 0;
    }
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;

    fs_lock();
    struct tmpfs_dirent *de = tmpfs_dirfind(td, name);
    if (!de) { fs_unlock(); return -ENOENT; }
    *out = inode_get(&de->target->vnode);
    fs_unlock();
    return 0;
}

static int tmpfs_op_getdents(struct inode *dir, uint64_t off, void *buf,
                              uint64_t n, uint64_t *out_next) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)dir;
    if (td->vnode.type != I_DIR) return -ENOTDIR;

    /* We expose ".", ".." and then real entries in dirent-list order.
     * `off` is just an opaque cursor: 0 = ".", 1 = "..", 2+ = list[off-2]. */
    uint64_t written = 0;
    uint64_t cursor = off;

    /* Helper: emit one dirent if it fits. Returns 1 on success, 0 if no room. */
    #define EMIT(name_str, ino_val, dtype) do {                          \
        int namelen = 0;                                                  \
        while ((name_str)[namelen]) namelen++;                            \
        namelen++;                                                        \
        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;             \
        if (written + reclen > n) goto done;                              \
        struct dirent64 *de = (struct dirent64 *)((char *)buf + written); \
        memset(de, 0, reclen);                                            \
        de->d_ino    = (uint64_t)(ino_val);                               \
        de->d_off    = cursor + 1;                                        \
        de->d_reclen = (uint16_t)reclen;                                  \
        de->d_type   = (dtype);                                           \
        for (int j = 0; j < namelen; j++) de->d_name[j] = (name_str)[j];  \
        written += reclen;                                                \
        cursor++;                                                         \
    } while (0)

    /* Hold fs_lock across the entire walk: timer preemption could
     * otherwise reschedule another process that calls unlink/rename
     * and page_free()s a dirent we still hold a pointer to. */
    fs_lock();

    struct tmpfs_inode *par = td->parent ? td->parent : td;
    if (cursor == 0) EMIT(".",  (uint64_t)(uintptr_t)td,  DT_DIR);
    if (cursor == 1) EMIT("..", (uint64_t)(uintptr_t)par, DT_DIR);

    /* Walk dirent list, skipping the first (cursor - 2) entries. */
    uint64_t skip = (cursor >= 2) ? cursor - 2 : 0;
    struct tmpfs_dirent *d = td->dirents;
    while (d && skip > 0) { d = d->next; skip--; }
    while (d) {
        if (d->target) {
            uint8_t dtype = (d->target->vnode.type == I_DIR) ? DT_DIR :
                            (d->target->vnode.type == I_LNK) ? DT_LNK :
                                                               DT_REG;
            EMIT(d->name, (uint64_t)(uintptr_t)d->target, dtype);
        }
        d = d->next;
    }

done:
    fs_unlock();
    if (out_next) *out_next = cursor;
    return (int)written;

    #undef EMIT
}

static int tmpfs_op_truncate(struct inode *ip) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    if (ti->vnode.type != I_REG) return -EINVAL;

    fs_lock();
    for (int i = 0; i < TMPFS_PAGES_PER_FILE; i++) {
        if (ti->file_pages[i]) {
            page_free(ti->file_pages[i]);
            ti->file_pages[i] = 0;
        }
    }
    ti->vnode.size     = 0;
    ti->vnode.mtime    = tmpfs_now();
    fs_unlock();
    return 0;
}

static void tmpfs_op_release(struct inode *ip) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    /* refcnt has just dropped to 0. Free the inode iff no name refers
     * to it any more (POSIX deferred unlink). */
    if (ti->in_use && ti->vnode.nlink == 0) {
        fs_lock();
        tmpfs_ifree(ti);
        fs_unlock();
    }
}

static int tmpfs_op_create(struct inode *parent, const char *name,
                            struct inode **out) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)parent;
    if (td->vnode.type != I_DIR) return -ENOTDIR;
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;

    fs_lock();
    if (tmpfs_dirfind(td, name)) { fs_unlock(); return -EEXIST; }
    struct tmpfs_inode *ni = tmpfs_ialloc(I_REG);
    if (!ni) { fs_unlock(); return -ENOSPC; }
    int rc = tmpfs_dirlink(td, name, ni);
    if (rc < 0) {
        tmpfs_ifree(ni);
        fs_unlock();
        return rc;
    }
    td->vnode.mtime = tmpfs_now();
    fs_unlock();
    *out = &ni->vnode;
    return 0;
}

static int tmpfs_op_mkdir(struct inode *parent, const char *name) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)parent;
    if (td->vnode.type != I_DIR) return -ENOTDIR;
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;

    fs_lock();
    if (tmpfs_dirfind(td, name)) { fs_unlock(); return -EEXIST; }
    struct tmpfs_inode *ni = tmpfs_ialloc(I_DIR);
    if (!ni) { fs_unlock(); return -ENOSPC; }
    /* New dirs have nlink == 2 ("." + parent's reference). */
    ni->vnode.nlink = 2;
    ni->parent      = td;

    int rc = tmpfs_dirlink(td, name, ni);
    if (rc < 0) {
        tmpfs_ifree(ni);
        fs_unlock();
        return rc;
    }
    /* Parent gains a back-reference (the new dir's ".."). */
    td->vnode.nlink++;
    td->vnode.mtime = tmpfs_now();
    fs_unlock();
    /* Drop the iget refcnt from ialloc — caller doesn't keep this inode. */
    inode_put(&ni->vnode);
    return 0;
}

static int tmpfs_op_unlink(struct inode *parent, const char *name) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)parent;
    if (td->vnode.type != I_DIR) return -ENOTDIR;
    /* "." and ".." are not unlinkable. */
    if (name[0] == '.' && (name[1] == 0 ||
        (name[1] == '.' && name[2] == 0))) return -EINVAL;
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;

    fs_lock();
    struct tmpfs_dirent *de = tmpfs_dirfind(td, name);
    if (!de) { fs_unlock(); return -ENOENT; }
    struct tmpfs_inode *target = de->target;

    if (target->vnode.type == I_DIR && !tmpfs_dir_is_empty(target)) {
        fs_unlock();
        return -ENOTEMPTY;
    }

    /* Remove the dirent first so nothing can look it up afterwards. */
    int rc = tmpfs_dirunlink(td, name);
    if (rc < 0) { fs_unlock(); return rc; }

    /* Update nlink. New dirs start at nlink == 2 ("." + parent's
     * entry), and the parent itself bumped its own nlink for the
     * subdir's ".." back-ref at mkdir time. So removing a dir drops
     * both of those: target -= 2 (dir entry + own "."), parent -= 1
     * (lost back-ref). For regular files just drop the entry. */
    if (target->vnode.type == I_DIR) {
        td->vnode.nlink--;
        target->vnode.nlink--;
    }
    target->vnode.nlink--;
    target->vnode.mtime = tmpfs_now();

    /* If no remaining links AND no open refs, free now. Otherwise leave
     * it; release() will clean up when the last fd closes. */
    if (target->vnode.nlink == 0 && target->vnode.refcnt == 0) {
        tmpfs_ifree(target);
    }
    td->vnode.mtime = tmpfs_now();
    fs_unlock();
    return 0;
}

static int tmpfs_op_link(struct inode *parent, struct inode *target_in,
                          const char *name) {
    struct tmpfs_inode *td  = (struct tmpfs_inode *)parent;
    struct tmpfs_inode *tgt = (struct tmpfs_inode *)target_in;
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;

    fs_lock();
    int rc = tmpfs_dirlink(td, name, tgt);
    if (rc < 0) { fs_unlock(); return rc; }
    tgt->vnode.nlink++;
    tgt->vnode.mtime = tmpfs_now();
    td->vnode.mtime  = tmpfs_now();
    fs_unlock();
    return 0;
}

static int tmpfs_op_rename(struct inode *old_p, const char *old_name,
                            struct inode *new_p, const char *new_name) {
    struct tmpfs_inode *od = (struct tmpfs_inode *)old_p;
    struct tmpfs_inode *nd = (struct tmpfs_inode *)new_p;
    if (!tmpfs_name_ok(old_name) || !tmpfs_name_ok(new_name))
        return -ENAMETOOLONG;

    fs_lock();
    /* Same-path no-op. */
    if (od == nd && name_eq(old_name, new_name)) { fs_unlock(); return 0; }

    struct tmpfs_dirent *src_de = tmpfs_dirfind(od, old_name);
    if (!src_de) { fs_unlock(); return -ENOENT; }
    struct tmpfs_inode *src = src_de->target;

    /* Loop check: if src is a dir, new_p must not be src nor any
     * descendant of src. Walk new_p's parent chain upward; if we hit
     * src before reaching the root, the move would create a cycle. */
    if (src->vnode.type == I_DIR) {
        for (struct tmpfs_inode *a = nd; a; a = a->parent) {
            if (a == src) { fs_unlock(); return -EINVAL; }
        }
    }

    struct tmpfs_dirent *dst_de = tmpfs_dirfind(nd, new_name);
    if (dst_de) {
        struct tmpfs_inode *dst = dst_de->target;
        if (src->vnode.type == I_DIR && dst->vnode.type != I_DIR) {
            fs_unlock(); return -ENOTDIR;
        }
        if (src->vnode.type != I_DIR && dst->vnode.type == I_DIR) {
            fs_unlock(); return -EISDIR;
        }
        if (dst->vnode.type == I_DIR && !tmpfs_dir_is_empty(dst)) {
            fs_unlock(); return -ENOTEMPTY;
        }
        /* Replace: unlink dst first. Same nlink accounting as
         * tmpfs_op_unlink — drop parent's back-ref + dst's "." for
         * directories, then the dirent link itself. */
        tmpfs_dirunlink(nd, new_name);
        if (dst->vnode.type == I_DIR) {
            nd->vnode.nlink--;
            dst->vnode.nlink--;
        }
        dst->vnode.nlink--;
        if (dst->vnode.nlink == 0 && dst->vnode.refcnt == 0)
            tmpfs_ifree(dst);
    }

    int rc = tmpfs_dirlink(nd, new_name, src);
    if (rc < 0) { fs_unlock(); return rc; }
    tmpfs_dirunlink(od, old_name);

    /* Cross-parent directory move: parent nlink fixup + parent ptr. */
    if (src->vnode.type == I_DIR && od != nd) {
        od->vnode.nlink--;
        nd->vnode.nlink++;
        src->parent = nd;
    }
    od->vnode.mtime = tmpfs_now();
    nd->vnode.mtime = tmpfs_now();
    src->vnode.mtime = tmpfs_now();
    fs_unlock();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Symbolic links                                                     */
/* ------------------------------------------------------------------ */

static int tmpfs_op_symlink(struct inode *parent, const char *name,
                            const char *target) {
    struct tmpfs_inode *td = (struct tmpfs_inode *)parent;
    if (td->vnode.type != I_DIR) return -ENOTDIR;
    if (!tmpfs_name_ok(name)) return -ENAMETOOLONG;
    if (!target || target[0] == '\0') return -EINVAL;

    /* Target must fit in one tmpfs page (4 KiB) including the trailing NUL.
     * Real PATH_MAX is 4096 so this is just defense against pathological
     * inputs that wouldn't survive readlink either. */
    uint64_t tlen = 0;
    while (target[tlen] && tlen < TMPFS_PAGE_SIZE) tlen++;
    if (tlen == TMPFS_PAGE_SIZE) return -ENAMETOOLONG;  /* unterminated within page */

    fs_lock();
    if (tmpfs_dirfind(td, name)) { fs_unlock(); return -EEXIST; }
    struct tmpfs_inode *ni = tmpfs_ialloc(I_LNK);
    if (!ni) { fs_unlock(); return -ENOSPC; }

    /* Store target in file_pages[0]. Bypasses the regular write path
     * (which sanity-checks I_REG); symlinks reuse the storage but not
     * the read/write ops. */
    void *page = page_alloc();
    if (!page) {
        tmpfs_ifree(ni);
        fs_unlock();
        return -ENOSPC;
    }
    for (uint64_t i = 0; i < tlen; i++) ((char *)page)[i] = target[i];
    ((char *)page)[tlen] = '\0';
    ni->file_pages[0] = page;
    ni->vnode.size = tlen;

    int rc = tmpfs_dirlink(td, name, ni);
    if (rc < 0) {
        page_free(page);
        ni->file_pages[0] = 0;
        tmpfs_ifree(ni);
        fs_unlock();
        return rc;
    }
    td->vnode.mtime = tmpfs_now();
    fs_unlock();
    /* tmpfs_dirlink doesn't bump nlink — and our caller will inode_put
     * the inode if we returned it. We don't return it, so drop the
     * ialloc'd refcnt to match (the dirent holds the alias). */
    inode_put(&ni->vnode);
    return 0;
}

static int tmpfs_op_readlink(struct inode *ip, char *buf, uint64_t n) {
    struct tmpfs_inode *ti = (struct tmpfs_inode *)ip;
    if (ti->vnode.type != I_LNK) return -EINVAL;
    fs_lock();
    void *page = ti->file_pages[0];
    uint64_t tlen = ti->vnode.size;
    if (!page || tlen == 0) { fs_unlock(); return -EINVAL; }
    uint64_t copy = tlen < n ? tlen : n;
    for (uint64_t i = 0; i < copy; i++) buf[i] = ((const char *)page)[i];
    fs_unlock();
    return (int)copy;
}

/* ------------------------------------------------------------------ */
/* Init + mount                                                       */
/* ------------------------------------------------------------------ */

void tmpfs_init(void) {
    /* Wipe the pool. */
    for (int i = 0; i < TMPFS_NINODES; i++) {
        memset(&inodes[i], 0, sizeof(inodes[i]));
    }
    /* Slot 0 is the root directory. */
    struct tmpfs_inode *root = &inodes[0];
    root->in_use         = 1;
    root->vnode.type     = I_DIR;
    root->vnode.mode     = S_IFDIR | 0777;
    root->vnode.nlink    = 2;          /* "." + (whatever mounts us)   */
    root->vnode.refcnt   = 0;          /* mount table holds borrowed ref */
    root->vnode.size     = 0;
    root->vnode.mtime    = tmpfs_now();
    root->vnode.ops      = &tmpfs_iops;
    root->vnode.fs_data  = root;
    tmpfs_ready = 1;
    printk("tmpfs: initialised (max %d inodes, %d KiB per file)\n",
           TMPFS_NINODES, TMPFS_MAX_FILESIZE / 1024);
}

int tmpfs_attach(const char *target) {
    if (!tmpfs_ready) return -ENODEV;
    /* mount_fs records a borrowed pointer; the root inode lives forever
     * in the static inodes[] pool, so no inode_get/put is needed. Doing
     * one here would leak a refcnt on the idempotent same-root re-mount
     * path (mount_fs returns 0 without consuming an extra ref). Mirrors
     * procfs_attach. */
    int rc = mount_fs(target, &inodes[0].vnode);
    if (rc < 0) return rc;
    printk("tmpfs: mounted %s\n", target);
    return 0;
}
