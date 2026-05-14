#include <tarfs.h>
#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <string.h>
#include <vfs.h>
#include <printk.h>

extern char _tarfs_start[], _tarfs_end[];

/* ----------------------------------------------------------------
 * Helpers shared by old and new code
 * ---------------------------------------------------------------- */

static unsigned long parse_octal(const char *s, int n) {
    unsigned long val = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        val = val * 8 + (s[i] - '0');
    return val;
}

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static const char *strip_prefix(const char *s) {
    if (s[0] == '.' && s[1] == '/') return s + 2;
    if (s[0] == '/') return s + 1;
    return s;
}

/* ----------------------------------------------------------------
 * Legacy flat lookup — kept for selftests and backward compat
 * ---------------------------------------------------------------- */

const void *tarfs_find(const char *path, unsigned long *out_size) {
    const char *target = strip_prefix(path);
    char *p = _tarfs_start;
    while (p + 512 <= _tarfs_end) {
        struct tar_header *h = (struct tar_header *)p;
        if (h->name[0] == '\0') break;
        unsigned long size = parse_octal(h->size, 12);
        char *data = p + 512;
        if (h->typeflag == '0' || h->typeflag == '\0') {
            const char *name = strip_prefix(h->name);
            if (streq(name, target)) {
                if (out_size) *out_size = size;
                return data;
            }
        }
        unsigned long data_blocks = (size + 511) / 512;
        p += 512 + data_blocks * 512;
    }
    return 0;
}

/* ================================================================
 * Phase 4c — inode tree
 * ================================================================ */

#define TARFS_MAX_INODES   1024
#define TARFS_MAX_CHILDREN 1024

struct tarfs_ino_data {
    const char        *data;      // regular file: pointer into archive blob
    uint64_t           file_size;
    struct tarfs_child *children; // directory: linked list of children
    struct inode       *parent;   // for ".." resolution
};

struct tarfs_child {
    char              name[101];
    struct inode     *ino;
    struct tarfs_child *next;
};

#define TARFS_MAX_SYMLINKS 128

static struct inode          pool_inodes  [TARFS_MAX_INODES];
static struct tarfs_ino_data pool_data    [TARFS_MAX_INODES];
static struct tarfs_child    pool_children[TARFS_MAX_CHILDREN];
static char                  symlink_pool [TARFS_MAX_SYMLINKS][101];
static int inode_count = 0;
static int child_count = 0;
static int symlink_count = 0;

static const char *tarfs_alloc_symlink(const char *raw, uint64_t *out_len) {
    if (symlink_count >= TARFS_MAX_SYMLINKS) {
        printk("tarfs: symlink pool exhausted\n");
        return 0;
    }
    char *slot = symlink_pool[symlink_count++];
    int n = 0;
    /* tar linkname is up to 100 bytes, may not be null-terminated. */
    while (n < 100 && raw[n]) { slot[n] = raw[n]; n++; }
    slot[n] = '\0';
    if (out_len) *out_len = (uint64_t)n;
    return slot;
}

struct inode *tarfs_root = 0;

/* Forward declarations */
static const struct inode_ops tarfs_ops;

/* ---- allocators ---- */

static struct inode *tarfs_alloc_inode(int type, uint32_t mode,
                                        uint64_t size, const char *data) {
    if (inode_count >= TARFS_MAX_INODES) {
        printk("tarfs: inode pool exhausted\n");
        return 0;
    }
    int i = inode_count++;
    struct inode          *ip = &pool_inodes[i];
    struct tarfs_ino_data *d  = &pool_data[i];

    ip->type        = type;
    ip->mode        = mode;
    ip->uid         = 0;
    ip->gid         = 0;
    ip->size        = size;
    ip->mtime       = 0;
    ip->nlink       = 1;
    ip->refcnt      = 1;
    ip->ops         = &tarfs_ops;
    ip->fs_data     = d;
    ip->mount_child = 0;
    ip->mount_parent = 0;

    d->data      = data;
    d->file_size = size;
    d->children  = 0;
    d->parent    = 0;
    return ip;
}

static struct tarfs_child *tarfs_alloc_child(const char *name,
                                              struct inode *ino) {
    if (child_count >= TARFS_MAX_CHILDREN) {
        printk("tarfs: child pool exhausted\n");
        return 0;
    }
    struct tarfs_child *c = &pool_children[child_count++];
    int n = 0;
    while (name[n] && n < 100) { c->name[n] = name[n]; n++; }
    c->name[n] = '\0';
    c->ino  = ino;
    c->next = 0;
    return c;
}

static void tarfs_dir_add(struct inode *dir, const char *name,
                           struct inode *child) {
    struct tarfs_ino_data *d = dir->fs_data;
    struct tarfs_child *c = tarfs_alloc_child(name, child);
    if (!c) return;
    // Append to tail so traversal order matches archive order.
    if (!d->children) {
        d->children = c;
    } else {
        struct tarfs_child *t = d->children;
        while (t->next) t = t->next;
        t->next = c;
    }
    ((struct tarfs_ino_data *)child->fs_data)->parent = dir;
}

/* Walk from root to ensure path exists as a directory chain.
 * Creates missing intermediate directories. Returns the final dir inode. */
static struct inode *tarfs_ensure_dir(const char *path) {
    // path is relative to archive root (no leading slash), e.g. "bin" or "usr/bin"
    struct inode *cur = tarfs_root;
    const char *p = path;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        // Extract component.
        char name[101];
        int  n = 0;
        while (*p && *p != '/' && n < 100) name[n++] = *p++;
        name[n] = '\0';
        if (!n) break;

        // Skip trailing slash on dir entries.
        if (name[n-1] == '\0' && !*p) break;

        // Look up name in cur's children.
        struct tarfs_ino_data *d = cur->fs_data;
        struct inode *found = 0;
        for (struct tarfs_child *c = d->children; c; c = c->next) {
            if (streq(c->name, name)) { found = c->ino; break; }
        }

        if (!found) {
            found = tarfs_alloc_inode(I_DIR, S_IFDIR | 0755, 0, 0);
            if (!found) return 0;
            tarfs_dir_add(cur, name, found);
        }
        cur = found;
    }
    return cur;
}

/* ---- inode ops ---- */

static int tarfs_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    return generic_file_read(ip, off, buf, n);
}

static int tarfs_write(struct inode *ip, uint64_t off, const void *buf,
                        uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EROFS;
}

static int tarfs_stat(struct inode *ip, struct stat *st) {
    struct tarfs_ino_data *d = ip->fs_data;
    st->st_dev   = 1;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = ip->nlink;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_size  = d->file_size;
    st->st_atime = ip->mtime;
    st->st_mtime = ip->mtime;
    st->st_ctime = ip->mtime;
    st->st_blksize = 512;
    st->st_blocks  = (d->file_size + 511) / 512;
    return 0;
}

static int tarfs_lookup(struct inode *dir, const char *name,
                         struct inode **out) {
    struct tarfs_ino_data *d = dir->fs_data;

    if (streq(name, ".")) {
        *out = inode_get(dir);
        return 0;
    }
    if (streq(name, "..")) {
        struct inode *parent = d->parent ? d->parent : dir;
        *out = inode_get(parent);
        return 0;
    }

    for (struct tarfs_child *c = d->children; c; c = c->next) {
        if (streq(c->name, name)) {
            *out = inode_get(c->ino);
            return 0;
        }
    }
    return -ENOENT;
}

static int tarfs_readlink(struct inode *ip, char *buf, uint64_t n) {
    if (ip->type != I_LNK) return -EINVAL;
    struct tarfs_ino_data *d = ip->fs_data;
    if (!d->data) return -EINVAL;
    uint64_t len = d->file_size;
    if (len > n) len = n;
    for (uint64_t i = 0; i < len; i++) buf[i] = d->data[i];
    return (int)len;
}

static int tarfs_getdents(struct inode *dir, uint64_t off, void *buf,
                           uint64_t n, uint64_t *out_next) {
    struct tarfs_ino_data *d = dir->fs_data;

    uint64_t written = 0;
    uint64_t cursor  = off;

    /* Emit one dirent if it fits; otherwise stop. `cursor` is an opaque
     * stream position: 0 = ".", 1 = "..", 2+ = children[cursor - 2]. */
    #define EMIT(name_str, ino_ptr, dtype) do {                          \
        int namelen = 0;                                                  \
        while ((name_str)[namelen]) namelen++;                            \
        namelen++;                                                        \
        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;             \
        if (written + (uint64_t)reclen > n) goto done;                    \
        struct dirent64 *de = (struct dirent64 *)((char *)buf + written); \
        de->d_ino    = (uint64_t)(uintptr_t)(ino_ptr);                    \
        de->d_off    = cursor + 1;                                        \
        de->d_reclen = (uint16_t)reclen;                                  \
        de->d_type   = (dtype);                                           \
        for (int i = 0; i < namelen; i++) de->d_name[i] = (name_str)[i];  \
        written += (uint64_t)reclen;                                      \
        cursor++;                                                         \
    } while (0)

    /* Mirror tarfs_lookup: ".." of the root resolves to the root itself. */
    if (cursor == 0) EMIT(".",  dir,                         DT_DIR);
    if (cursor == 1) EMIT("..", d->parent ? d->parent : dir, DT_DIR);

    /* Walk to children[cursor - 2]. */
    uint64_t skip = (cursor >= 2) ? cursor - 2 : 0;
    struct tarfs_child *c = d->children;
    while (c && skip > 0) { c = c->next; skip--; }

    while (c) {
        EMIT(c->name, c->ino,
             (c->ino->type == I_DIR) ? DT_DIR :
             (c->ino->type == I_CHR) ? DT_CHR :
             (c->ino->type == I_LNK) ? DT_LNK : DT_REG);
        c = c->next;
    }

done:
    if (out_next) *out_next = cursor;
    return (int)written;

    #undef EMIT
}

static int tarfs_readpage(struct inode *ip, uint64_t pgidx, void *page) {
    struct tarfs_ino_data *d = ip->fs_data;
    uint64_t off = pgidx * 4096UL;
    if (off >= d->file_size) {
        memset(page, 0, 4096);
        return 0;
    }
    uint64_t n = 4096;
    if (off + n > d->file_size) n = d->file_size - off;
    memcpy(page, d->data + off, n);
    if (n < 4096) memset((char *)page + n, 0, 4096 - n);
    return 0;
}

static const struct inode_ops tarfs_ops = {
    .read     = tarfs_read,
    .write    = tarfs_write,
    .stat     = tarfs_stat,
    .lookup   = tarfs_lookup,
    .getdents = tarfs_getdents,
    .readpage = tarfs_readpage,
    .readlink = tarfs_readlink,
};

/* ================================================================
 * tarfs_init — build the inode tree and mount at "/"
 * ================================================================ */

void tarfs_init(void) {
    // Root directory inode.
    tarfs_root = tarfs_alloc_inode(I_DIR, S_IFDIR | 0755, 0, 0);
    if (!tarfs_root) { printk("tarfs_init: OOM\n"); return; }
    ((struct tarfs_ino_data *)tarfs_root->fs_data)->parent = tarfs_root;

    // Pre-create stubs for known mount points so devfs can attach.
    tarfs_ensure_dir("dev");
    tarfs_ensure_dir("bin");
    tarfs_ensure_dir("etc");
    tarfs_ensure_dir("mnt");
    tarfs_ensure_dir("proc");

    // Walk the archive and build the tree.
    char *p = _tarfs_start;
    while (p + 512 <= _tarfs_end) {
        struct tar_header *h = (struct tar_header *)p;
        if (h->name[0] == '\0') break;

        uint64_t size = parse_octal(h->size, 12);
        char *data    = p + 512;

        const char *raw  = strip_prefix(h->name);
        uint32_t    mode = (uint32_t)(parse_octal(h->mode, 8) & 0777);
        uint64_t    mtime= parse_octal(h->mtime, 12);

        if (h->typeflag == '5') {
            // Explicit directory entry (e.g. "bin/").
            // tarfs_ensure_dir will walk/create intermediate dirs.
            // Strip trailing slash from path before ensuring.
            char dpath[256];
            int n = 0;
            while (raw[n] && n < 254) { dpath[n] = raw[n]; n++; }
            // Remove trailing slash.
            if (n > 0 && dpath[n-1] == '/') n--;
            dpath[n] = '\0';
            if (n > 0) {
                struct inode *dir = tarfs_ensure_dir(dpath);
                if (dir) {
                    dir->mode  = S_IFDIR | mode;
                    dir->mtime = mtime;
                }
            }
        } else if (h->typeflag == '0' || h->typeflag == '\0') {
            // Regular file: find/create parent dir, insert file inode.
            char fpath[256];
            int n = 0;
            while (raw[n] && n < 254) { fpath[n] = raw[n]; n++; }
            fpath[n] = '\0';

            // Split into dirname + basename.
            int slash = -1;
            for (int i = n - 1; i >= 0; i--) {
                if (fpath[i] == '/') { slash = i; break; }
            }

            const char *basename;
            struct inode *parent;

            if (slash < 0) {
                basename = fpath;
                parent   = tarfs_root;
            } else {
                fpath[slash] = '\0';
                basename     = fpath + slash + 1;
                parent       = tarfs_ensure_dir(fpath);
            }

            if (parent && basename[0]) {
                struct inode *fip = tarfs_alloc_inode(I_REG,
                                        S_IFREG | mode, size, data);
                if (fip) {
                    fip->mtime = mtime;
                    tarfs_dir_add(parent, basename, fip);
                }
            }
        } else if (h->typeflag == '2') {
            /* Symbolic link. linkname (header field) holds the target;
             * stash a null-terminated copy in the symlink pool and point
             * the inode's data at it. */
            char fpath[256];
            int n = 0;
            while (raw[n] && n < 254) { fpath[n] = raw[n]; n++; }
            fpath[n] = '\0';

            int slash = -1;
            for (int i = n - 1; i >= 0; i--) {
                if (fpath[i] == '/') { slash = i; break; }
            }

            const char *basename;
            struct inode *parent;
            if (slash < 0) {
                basename = fpath;
                parent   = tarfs_root;
            } else {
                fpath[slash] = '\0';
                basename     = fpath + slash + 1;
                parent       = tarfs_ensure_dir(fpath);
            }

            if (parent && basename[0]) {
                uint64_t tlen = 0;
                const char *target = tarfs_alloc_symlink(h->linkname, &tlen);
                if (target) {
                    struct inode *lip = tarfs_alloc_inode(I_LNK,
                                            S_IFLNK | 0777, tlen, target);
                    if (lip) {
                        lip->mtime = mtime;
                        tarfs_dir_add(parent, basename, lip);
                    }
                }
            }
        }
        /* Hard links ('1') still skipped. */

        unsigned long data_blocks = (size + 511) / 512;
        p += 512 + data_blocks * 512;
    }

    mount_fs("/", tarfs_root);
    printk("tarfs: mounted %d inodes\n", inode_count);
}
