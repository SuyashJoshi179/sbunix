#include <errno.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <pipe.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <resource.h>
#include <riscv.h>
#include <procfs.h>
#include <sbfs.h>
#include <tmpfs.h>
#include <signal.h>
#include <drivers/rtc.h>
#include <stat.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>
#include <tarfs.h>
#include <time.h>
#include <timer.h>
#include <vfs.h>
#include <vma.h>
#include <vmem.h>
#include <log.h>
#include <page_cache.h>
#include <drivers/uart.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

enum {
    UIO_CHUNK = 256,
    PATH_MAX_LOCAL = 256,
    ARGV_MAX_LOCAL = 32,
};

static int copyin_cstr(const char *usrc, char *kdst, unsigned long cap) {
    if (!usrc || !kdst || cap == 0) return -EFAULT;
    for (unsigned long i = 0; i < cap; i++) {
        char c;
        if (copyin(&c, usrc + i, 1) < 0) return -EFAULT;
        kdst[i] = c;
        if (c == '\0') return 0;
    }
    kdst[cap - 1] = '\0';
    return -ENAMETOOLONG;
}

static int proc_fd_limit(const struct pcb *p) {
    if (!p) return NOFILE;
    rlim_t lim = p->rlim[RLIMIT_NOFILE].rlim_cur;
    if (lim == RLIM_INFINITY || lim > (rlim_t)NOFILE) return NOFILE;
    return (int)lim;
}

static int proc_open_fd_count(const struct pcb *p) {
    int n = 0;
    if (!p) return 0;
    for (int fd = 0; fd < NOFILE; fd++)
        if (p->ofile[fd]) n++;
    return n;
}

static int __attribute__((unused)) proc_vma_count(const struct pcb *p) {
    int n = 0;
    if (!p) return 0;
    for (struct vma *v = p->vma_list; v; v = v->next)
        n++;
    return n;
}

// Allocate the lowest free fd slot in the current process.
// Returns the fd number or -EMFILE if the table is full.
static int alloc_fd(struct pcb *p, struct file *f) {
    for (int fd = 0; fd < NOFILE; fd++) {
        if (!p->ofile[fd]) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -EMFILE;
}

// ---------------------------------------------------------------------------
// sys_exit
// ---------------------------------------------------------------------------
static int64_t sys_exit(int status) {
    proc_exit_current((status & 0xff) << 8);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_write — write through the FD table
//
// Falls back to direct UART for fd 1/2 if the process has no ofile table
// (kernel threads, early-boot selftests).
// ---------------------------------------------------------------------------
static int64_t sys_write(int fd, const char *buf, uint64_t len) {
    if (len > 0 && !buf) return -EFAULT;

    struct pcb *p = current_proc();
    char kbuf[UIO_CHUNK];
    uint64_t done = 0;

    if (p && fd >= 0 && fd < NOFILE && p->ofile[fd]) {
        while (done < len) {
            uint64_t chunk = len - done;
            if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
            if (copyin(kbuf, buf + done, chunk) < 0)
                return done > 0 ? (int64_t)done : -EFAULT;
            int w = filewrite(p->ofile[fd], kbuf, chunk);
            if (w < 0) return done > 0 ? (int64_t)done : w;
            if (w == 0) break;
            done += (uint64_t)w;
            if ((uint64_t)w < chunk) break;
        }
        return (int64_t)done;
    }

    // Fallback: direct UART for fd 1/2 (handles early boot and kernel threads).
    if (fd == 1 || fd == 2) {
        while (done < len) {
            uint64_t chunk = len - done;
            if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
            if (copyin(kbuf, buf + done, chunk) < 0)
                return done > 0 ? (int64_t)done : -EFAULT;
            for (uint64_t i = 0; i < chunk; i++) write_char(kbuf[i]);
            done += chunk;
        }
        return (int64_t)done;
    }
    return -EBADF;
}

// ---------------------------------------------------------------------------
// sys_read — read through the FD table
// ---------------------------------------------------------------------------
static int64_t sys_read(int fd, void *buf, uint64_t len) {
    if (len > 0 && !buf) return -EFAULT;

    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;

    char kbuf[UIO_CHUNK];
    uint64_t done = 0;
    while (done < len) {
        uint64_t chunk = len - done;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
        int r = fileread(p->ofile[fd], kbuf, chunk);
        if (r < 0) return done > 0 ? (int64_t)done : r;
        if (r == 0) break;
        if (copyout((char *)buf + done, kbuf, (unsigned long)r) < 0)
            return done > 0 ? (int64_t)done : -EFAULT;
        done += (uint64_t)r;
        if ((uint64_t)r < chunk) break;
    }
    return (int64_t)done;
}

// ---------------------------------------------------------------------------
// path_split — split an absolute or relative path into parent dir path +
// leaf name.  parent_buf must hold at least the length of path + 2 bytes.
// Strips trailing '/' (POSIX: "/foo/" is equivalent to "/foo"), so `path`
// must be writable.  Returns 0 on success, -EINVAL if path has no leaf
// component (e.g. "" or "/" or "////").
//
// Relative-path handling:
//   "foo"      → parent ".",  leaf "foo"
//   "foo/bar"  → parent "foo", leaf "bar"
//   "./foo"    → parent ".",  leaf "foo"
// Callers pass parent_buf to namei(), which resolves relative parents from
// the process cwd — consistent with how namei() handles relative paths.
// ---------------------------------------------------------------------------
static int path_split(char *path, char *parent_buf, const char **leaf_out) {
    int len = 0;
    while (path[len]) len++;
    if (len == 0) return -EINVAL;

    // Strip trailing slashes, but never reduce "/" itself to "".
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    // After stripping, only absolute "/" alone has no leaf;
    // single-char relative names (e.g. "a") are valid.
    if (len == 1 && path[0] == '/') return -EINVAL;

    // Find last '/'.
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    if (last_slash < 0) {
        // No slash: bare relative name — parent is cwd (".").
        parent_buf[0] = '.';
        parent_buf[1] = '\0';
        *leaf_out = path;
        return 0;
    }

    // parent = path[0..last_slash) — or "/" if last_slash == 0
    int plen = last_slash == 0 ? 1 : last_slash;
    for (int i = 0; i < plen; i++) parent_buf[i] = path[i];
    parent_buf[plen] = '\0';

    *leaf_out = &path[last_slash + 1];
    return 0;
}

// ---------------------------------------------------------------------------
// sys_open (Phase 5: handles O_CREAT on writable sbfs files)
// ---------------------------------------------------------------------------
static int64_t sys_open(const char *path, int flags) {
    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (proc_open_fd_count(p) >= proc_fd_limit(p)) return -EMFILE;

    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    struct inode *ip = 0;
    int rc = namei(kpath, &ip);

    if (rc == -ENOENT && (flags & 0100 /* O_CREAT */)) {
        // Create the file.  Walk to the parent directory, then dispatch
        // through the parent fs's create op (NULL → read-only fs → EROFS).
        char parent_path[PATH_MAX_LOCAL];
        const char *leaf = 0;
        if (path_split(kpath, parent_path, &leaf) < 0) return -EINVAL;
        if (!leaf || !leaf[0]) return -EINVAL;

        struct inode *parent = 0;
        /* Propagate namei's -errno verbatim — collapsing all walk
         * failures to -ENOENT would mask -ENOTDIR / -EACCES / -ELOOP /
         * -ENAMETOOLONG from userspace. */
        int nrc = namei(parent_path, &parent);
        if (nrc < 0) return nrc;
        if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }
        if (!parent->ops || !parent->ops->create) {
            inode_put(parent);
            return -EROFS;
        }

        int crc = parent->ops->create(parent, leaf, &ip);
        inode_put(parent);
        if (crc < 0) return crc;
    } else if (rc < 0) {
        return rc;
    }

    struct file *f = filealloc();
    if (!f) { inode_put(ip); return -EMFILE; }

    f->type     = FD_INODE;
    f->ip       = ip;
    f->off      = 0;
    f->readable = ((flags & 3) == 0 || (flags & 3) == 2) ? 1 : 0;
    f->writable = ((flags & 3) == 1 || (flags & 3) == 2) ? 1 : 0;
    if (ip->type == I_CHR) { f->readable = 1; f->writable = 1; }
    // O_APPEND: start writes at end
    if (flags & 02000) f->off = ip->size;
    // O_TRUNC: truncate to zero length, freeing any data blocks.
    if ((flags & 01000) && f->writable && ip->type == I_REG && ip->ops->truncate) {
        ip->ops->truncate(ip);
    }

    int fd = alloc_fd(p, f);
    if (fd < 0) { fileclose(f); return fd; }
    return fd;
}

// ---------------------------------------------------------------------------
// sys_mkdir — dispatch through parent fs's mkdir op
// ---------------------------------------------------------------------------
static int64_t sys_mkdir(const char *path) {
    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    char parent_path[PATH_MAX_LOCAL];
    const char *leaf = 0;
    if (path_split(kpath, parent_path, &leaf) < 0) return -EINVAL;
    if (!leaf || !leaf[0]) return -EINVAL;

    struct inode *parent = 0;
    /* Propagate namei's -errno verbatim (see sys_mknod for rationale). */
    int nrc = namei(parent_path, &parent);
    if (nrc < 0) return nrc;
    if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }

    // Check existence first: EEXIST takes priority over EROFS so that
    // mkdir -p style callers on a read-only fs get the right error.
    if (parent->ops && parent->ops->lookup) {
        struct inode *existing = 0;
        if (parent->ops->lookup(parent, leaf, &existing) == 0) {
            inode_put(existing);
            inode_put(parent);
            return -EEXIST;
        }
    }

    if (!parent->ops || !parent->ops->mkdir) { inode_put(parent); return -EROFS; }

    int rc = parent->ops->mkdir(parent, leaf);
    inode_put(parent);
    return rc;
}

// ---------------------------------------------------------------------------
// sys_unlink — dispatch through parent fs's unlink op
// ---------------------------------------------------------------------------
static int64_t sys_unlink(const char *path) {
    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    char parent_path[PATH_MAX_LOCAL];
    const char *leaf = 0;
    if (path_split(kpath, parent_path, &leaf) < 0) return -EINVAL;
    if (!leaf || !leaf[0]) return -EINVAL;

    struct inode *parent = 0;
    /* Propagate namei's -errno verbatim. */
    int nrc = namei(parent_path, &parent);
    if (nrc < 0) return nrc;
    if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }
    if (!parent->ops || !parent->ops->unlink) { inode_put(parent); return -EROFS; }

    int rc = parent->ops->unlink(parent, leaf);
    inode_put(parent);
    return rc;
}

// ---------------------------------------------------------------------------
// sys_link — create newpath as a hard link to oldpath.
//
// POSIX rules implemented:
//   - oldpath must exist                              → -ENOENT
//   - target must not be a directory                  → -EPERM
//   - newpath's parent must be a directory            → -ENOTDIR
//   - target and newpath must be on the same fs      → -EXDEV
//   - newpath must not already exist                  → -EEXIST
//   - target's filesystem must support link           → -EROFS
// ---------------------------------------------------------------------------
static int64_t sys_link(const char *oldpath, const char *newpath) {
    char kold[PATH_MAX_LOCAL], knew[PATH_MAX_LOCAL];
    int rc = copyin_cstr(oldpath, kold, sizeof(kold));
    if (rc < 0) return rc;
    rc = copyin_cstr(newpath, knew, sizeof(knew));
    if (rc < 0) return rc;

    // Resolve target.
    struct inode *target = 0;
    /* Propagate namei's -errno (could be -ELOOP, -ENOTDIR, etc). */
    {
        int nrc = namei(kold, &target);
        if (nrc < 0) return nrc;
    }
    if (target->type == I_DIR) {
        inode_put(target);
        return -EPERM;
    }

    // Resolve parent of newpath.
    char parent_path[PATH_MAX_LOCAL];
    const char *leaf = 0;
    if (path_split(knew, parent_path, &leaf) < 0) {
        inode_put(target);
        return -EINVAL;
    }
    if (!leaf || !leaf[0]) {
        inode_put(target);
        return -EINVAL;
    }

    struct inode *parent = 0;
    /* Propagate namei's -errno verbatim. */
    int nrc = namei(parent_path, &parent);
    if (nrc < 0) {
        inode_put(target);
        return nrc;
    }
    if (parent->type != I_DIR) {
        inode_put(parent);
        inode_put(target);
        return -ENOTDIR;
    }

    // Cross-filesystem hard link is meaningless: a dirent stores an inum
    // that is only valid in its own filesystem's inode table.
    if (parent->ops != target->ops) {
        inode_put(parent);
        inode_put(target);
        return -EXDEV;
    }

    if (!parent->ops || !parent->ops->link) {
        inode_put(parent);
        inode_put(target);
        return -EROFS;
    }

    // EEXIST takes priority over later checks.
    if (parent->ops->lookup) {
        struct inode *existing = 0;
        if (parent->ops->lookup(parent, leaf, &existing) == 0) {
            inode_put(existing);
            inode_put(parent);
            inode_put(target);
            return -EEXIST;
        }
    }

    int r = parent->ops->link(parent, target, leaf);
    inode_put(parent);
    inode_put(target);
    return r;
}

// ---------------------------------------------------------------------------
// sys_symlink — create a symbolic link at `linkpath` whose target string
// is `target`. Unlike hard link, target is NOT resolved: it's stored
// literally and only walked when something later traverses the link.
//
// Errors: -ENOENT (parent dir missing), -EEXIST (linkpath exists),
// -ENOTDIR (parent not a dir), -EROFS (fs lacks symlink op),
// -EINVAL (empty target / bad path), -ENAMETOOLONG (target too long).
// ---------------------------------------------------------------------------
static int64_t sys_symlink(const char *u_target, const char *u_linkpath) {
    char ktarget[PATH_MAX_LOCAL], klinkpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(u_target, ktarget, sizeof(ktarget));
    if (rc < 0) return rc;
    rc = copyin_cstr(u_linkpath, klinkpath, sizeof(klinkpath));
    if (rc < 0) return rc;
    if (ktarget[0] == '\0') return -EINVAL;

    char parent_path[PATH_MAX_LOCAL];
    const char *leaf = 0;
    if (path_split(klinkpath, parent_path, &leaf) < 0) return -EINVAL;
    if (!leaf || !leaf[0]) return -EINVAL;

    struct inode *parent = 0;
    /* Propagate namei's -errno verbatim. */
    int nrc = namei(parent_path, &parent);
    if (nrc < 0) return nrc;
    if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }

    if (parent->ops && parent->ops->lookup) {
        struct inode *existing = 0;
        if (parent->ops->lookup(parent, leaf, &existing) == 0) {
            inode_put(existing);
            inode_put(parent);
            return -EEXIST;
        }
    }

    if (!parent->ops || !parent->ops->symlink) {
        inode_put(parent);
        return -EROFS;
    }

    int r = parent->ops->symlink(parent, leaf, ktarget);
    inode_put(parent);
    return r;
}

// ---------------------------------------------------------------------------
// sys_rename — atomically move oldpath to newpath.
//
// POSIX rules implemented:
//   - oldpath must exist                              → -ENOENT
//   - both parents must be directories                → -ENOTDIR
//   - both paths must be on the same fs              → -EXDEV
//   - newpath's filesystem must support rename        → -EROFS
//   - rename of dir into its own subtree              → -EINVAL  (loop)
//   - file replacing dir / dir replacing file         → -EISDIR / -ENOTDIR
//   - non-empty dir target                            → -ENOTEMPTY
//   - same path same name                             → 0  (no-op)
// ---------------------------------------------------------------------------
static int64_t sys_rename(const char *oldpath, const char *newpath) {
    char kold[PATH_MAX_LOCAL], knew[PATH_MAX_LOCAL];
    int rc = copyin_cstr(oldpath, kold, sizeof(kold));
    if (rc < 0) return rc;
    rc = copyin_cstr(newpath, knew, sizeof(knew));
    if (rc < 0) return rc;

    // Split both paths into (parent, leaf).
    char old_parent_buf[PATH_MAX_LOCAL];
    char new_parent_buf[PATH_MAX_LOCAL];
    const char *old_leaf = 0, *new_leaf = 0;
    if (path_split(kold, old_parent_buf, &old_leaf) < 0) return -EINVAL;
    if (path_split(knew, new_parent_buf, &new_leaf) < 0) return -EINVAL;
    if (!old_leaf || !old_leaf[0]) return -EINVAL;
    if (!new_leaf || !new_leaf[0]) return -EINVAL;

    // Resolve both parents. Propagate namei -errno verbatim.
    struct inode *old_p = 0;
    {
        int nrc = namei(old_parent_buf, &old_p);
        if (nrc < 0) return nrc;
    }
    if (old_p->type != I_DIR) {
        inode_put(old_p);
        return -ENOTDIR;
    }

    struct inode *new_p = 0;
    {
        int nrc = namei(new_parent_buf, &new_p);
        if (nrc < 0) {
            inode_put(old_p);
            return nrc;
        }
    }
    if (new_p->type != I_DIR) {
        inode_put(new_p);
        inode_put(old_p);
        return -ENOTDIR;
    }

    // Cross-filesystem rename is impossible (different inum spaces).
    if (old_p->ops != new_p->ops) {
        inode_put(new_p);
        inode_put(old_p);
        return -EXDEV;
    }

    if (!new_p->ops || !new_p->ops->rename) {
        inode_put(new_p);
        inode_put(old_p);
        return -EROFS;
    }

    int r = new_p->ops->rename(old_p, old_leaf, new_p, new_leaf);
    inode_put(new_p);
    inode_put(old_p);
    return r;
}

// ---------------------------------------------------------------------------
// sys_close
// ---------------------------------------------------------------------------
static int64_t sys_close(int fd) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    fileclose(p->ofile[fd]);
    p->ofile[fd] = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_dup
// ---------------------------------------------------------------------------
static int64_t sys_dup(int fd) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    if (proc_open_fd_count(p) >= proc_fd_limit(p)) return -EMFILE;
    struct file *f = filedup(p->ofile[fd]);
    int newfd = alloc_fd(p, f);
    if (newfd < 0) { fileclose(f); return newfd; }
    return newfd;
}

// ---------------------------------------------------------------------------
// sys_dup2
// ---------------------------------------------------------------------------
static int64_t sys_dup2(int oldfd, int newfd) {
    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (oldfd < 0 || oldfd >= NOFILE || !p->ofile[oldfd]) return -EBADF;
    if (newfd < 0 || newfd >= NOFILE) return -EBADF;

    if (oldfd == newfd) return newfd;

    if (newfd >= proc_fd_limit(p)) return -EBADF;
    if (!p->ofile[newfd] && proc_open_fd_count(p) >= proc_fd_limit(p))
        return -EMFILE;

    if (p->ofile[newfd]) fileclose(p->ofile[newfd]);
    p->ofile[newfd] = filedup(p->ofile[oldfd]);
    return newfd;
}

// ---------------------------------------------------------------------------
// sys_lseek
// ---------------------------------------------------------------------------
static int64_t sys_lseek(int fd, int64_t off, int whence) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    return fileseek(p->ofile[fd], off, whence);
}

// ---------------------------------------------------------------------------
// sys_fstat
// ---------------------------------------------------------------------------
static int64_t sys_fstat(int fd, struct stat *st) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    struct stat kst;
    memset(&kst, 0, sizeof(kst));   /* fs ops only set the fields they care about */
    int rc = filestat(p->ofile[fd], &kst);
    if (rc < 0) return rc;
    if (copyout(st, &kst, (unsigned long)sizeof(kst)) < 0) return -EFAULT;
    return rc;
}

// ---------------------------------------------------------------------------
// sys_readlink
// ---------------------------------------------------------------------------
static int64_t sys_readlink(const char *path, char *buf, uint64_t n) {
    if (n > 0 && !buf) return -EFAULT;

    char kpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc < 0) return rc;

    struct inode *ip;
    rc = lnamei(kpath, &ip);
    if (rc < 0) return rc;

    if (ip->type != I_LNK || !ip->ops || !ip->ops->readlink) {
        inode_put(ip);
        return -EINVAL;
    }

    char kbuf[PATH_MAX_LOCAL];
    uint64_t cap = n < sizeof(kbuf) ? n : sizeof(kbuf);
    int got = ip->ops->readlink(ip, kbuf, cap);
    inode_put(ip);
    if (got < 0) return got;
    if ((uint64_t)got > cap) return -EIO;

    if (got > 0 && copyout(buf, kbuf, (unsigned long)got) < 0) return -EFAULT;
    return got;
}

// ---------------------------------------------------------------------------
// sys_lstat
// ---------------------------------------------------------------------------
static int64_t sys_lstat(const char *path, struct stat *st) {
    char kpath[PATH_MAX_LOCAL];
    int rc = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc < 0) return rc;

    struct inode *ip;
    rc = lnamei(kpath, &ip);
    if (rc < 0) return rc;

    if (!ip->ops || !ip->ops->stat) {
        inode_put(ip);
        return -EINVAL;
    }

    struct stat kst;
    memset(&kst, 0, sizeof(kst));
    rc = ip->ops->stat(ip, &kst);
    inode_put(ip);
    if (rc < 0) return rc;

    if (copyout(st, &kst, (unsigned long)sizeof(kst)) < 0) return -EFAULT;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_getdents64
// ---------------------------------------------------------------------------
static int64_t sys_getdents64(int fd, void *buf, uint64_t n) {
    if (n > 0 && !buf) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;

    struct file *f = p->ofile[fd];
    if (f->type != FD_INODE || !f->ip || f->ip->type != I_DIR) return -ENOTDIR;

    char kbuf[1024];
    uint64_t chunk = n;
    if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
    uint64_t next;
    int r = f->ip->ops->getdents(f->ip, f->off, kbuf, chunk, &next);
    if (r > 0) f->off = next;
    if (r > 0 && copyout(buf, kbuf, (unsigned long)r) < 0) return -EFAULT;
    return r;
}

// ---------------------------------------------------------------------------
// sys_chdir
// ---------------------------------------------------------------------------
static int64_t sys_chdir(const char *path) {
    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    struct inode *ip;
    int rc = namei(kpath, &ip);
    if (rc < 0) return rc;
    if (ip->type != I_DIR) { inode_put(ip); return -ENOTDIR; }

    struct pcb *p = current_proc();
    if (!p) { inode_put(ip); return -EBADF; }

    if (p->cwd) inode_put(p->cwd);
    p->cwd = ip;

    // Update cwd_path string with proper normalization. Walk each
    // component of the combined path and handle "." / "..":
    //   "."   — no-op
    //   ".."  — pop trailing component (but never past "/")
    //   other — append "/component"
    char norm[256];
    int nl = 0;
    if (kpath[0] != '/') {
        // Seed with current cwd_path.
        while (p->cwd_path[nl] && nl < 255) { norm[nl] = p->cwd_path[nl]; nl++; }
    }
    norm[nl] = '\0';

    int i = 0;
    while (kpath[i]) {
        while (kpath[i] == '/') i++;
        if (!kpath[i]) break;
        int start = i;
        while (kpath[i] && kpath[i] != '/') i++;
        int complen = i - start;

        if (complen == 1 && kpath[start] == '.') {
            continue;   // "."
        }
        if (complen == 2 && kpath[start] == '.' && kpath[start+1] == '.') {
            // Pop last component of norm.
            if (nl > 1) {
                nl--;
                while (nl > 0 && norm[nl] != '/') nl--;
                if (nl == 0) nl = 1;   // keep leading "/"
            } else if (nl == 0) {
                // no leading slash yet — drop nothing (at root already)
                nl = 1;
                norm[0] = '/';
            }
            norm[nl] = '\0';
            continue;
        }
        // Normal component — append "/comp".
        if (nl == 0 || norm[nl - 1] != '/') {
            if (nl < 255) norm[nl++] = '/';
        }
        for (int k = 0; k < complen && nl < 255; k++)
            norm[nl++] = kpath[start + k];
        norm[nl] = '\0';
    }

    if (nl == 0) { norm[0] = '/'; norm[1] = '\0'; nl = 1; }

    // Trim trailing slash except for root.
    if (nl > 1 && norm[nl - 1] == '/') { norm[--nl] = '\0'; }

    for (int k = 0; k <= nl && k < 255; k++) p->cwd_path[k] = norm[k];
    p->cwd_path[255] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// sys_getcwd
// ---------------------------------------------------------------------------
static int64_t sys_getcwd(char *buf, uint64_t n) {
    if (n > 0 && !buf) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EBADF;

    const char *cwd = p->cwd_path[0] ? p->cwd_path : "/";
    int len = 0;
    while (cwd[len]) len++;
    len++; // include null terminator

    if ((uint64_t)len > n) return -ERANGE;
    if (copyout(buf, cwd, (unsigned long)len) < 0) return -EFAULT;
    return len - 1;
}

// ---------------------------------------------------------------------------
// sys_getpid
// ---------------------------------------------------------------------------
static int64_t sys_getpid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->pid : -1;
}

// ---------------------------------------------------------------------------
// setup_user_stack — build argc/argv on a fresh user stack page.
//
// If argv_user is NULL, sets up an empty frame (argc=0).
// If non-NULL, copies strings from the OLD user address space (still active
// when called from sys_execv before switching page tables) to the new stack.
//
// Returns the user SP to use, or 0 on error.
// ---------------------------------------------------------------------------
static unsigned long setup_user_stack(void *kstack, char *const *argv_user) {
    char *base = (char *)kstack;
    char *top  = base + 4096;

    if (!argv_user) {
        uint64_t *frame = (uint64_t *)(top - 24);
        frame[0] = 0;  // argc
        frame[1] = 0;  // argv[0] = NULL
        frame[2] = 0;  // envp[0] = NULL
        return USER_STACK_TOP - 24;
    }

    // Count args and copy strings to bottom of stack page.
    int argc = 0;
    char *strp = base;
    unsigned long uaddrs[ARGV_MAX_LOCAL];

    for (int i = 0; i < ARGV_MAX_LOCAL; i++) {
        uint64_t uarg = 0;
        if (copyin(&uarg,
                   (const char *)argv_user + i * sizeof(uint64_t),
                   sizeof(uint64_t)) < 0)
            return 0;
        if (uarg == 0) break;

        int len = 0;
        char c = 0;
        do {
            if (copyin(&c, (const char *)(uintptr_t)(uarg + (uint64_t)len), 1) < 0)
                return 0;
            len++;
        } while (c && len < 256);
        if (c != 0) return 0;

        if (strp + len > top - (long)sizeof(uint64_t) * (argc + 4))
            break; // out of space
        if (copyin(strp, (const void *)(uintptr_t)uarg, (unsigned long)len) < 0)
            return 0;
        // Compute the user VA for this string
        uaddrs[argc] = (USER_STACK_TOP - 4096) + (unsigned long)(strp - base);
        strp += len;
        argc++;
    }

    // Build frame at top of page (growing down):
    // [argc] [argv[0]] ... [argv[argc-1]] [NULL] [NULL(envp)]
    int nslots = 1 + argc + 1 + 1;  // argc + pointers + NULL + envp NULL
    uint64_t *frame = (uint64_t *)(top - nslots * 8);
    // Alignment
    frame = (uint64_t *)((unsigned long)frame & ~7UL);

    frame[0] = (uint64_t)argc;
    for (int i = 0; i < argc; i++)
        frame[1 + i] = uaddrs[i];
    frame[1 + argc] = 0;  // argv[argc] = NULL
    frame[2 + argc] = 0;  // envp[0] = NULL

    unsigned long frame_off = (unsigned long)((char *)frame - base);
    return (USER_STACK_TOP - 4096) + frame_off;
}

// ---------------------------------------------------------------------------
// do_exec — shared exec logic for both SYS_exec and SYS_execv
// ---------------------------------------------------------------------------
static int64_t do_exec(const char *path, char *const *argv_user,
                       uint64_t *trapframe) {
    struct pcb *p = current_proc();
    if (!p || !p->is_user) return -EINVAL;
    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    /* Capture argv[0] for p->comm (surfaced via /proc/<pid>/status and
     * /proc/<pid>/stat). Read from the OLD address space while it is
     * still mapped; commit to p->comm only at the exec commit point so
     * a later -ENOMEM leaves comm reflecting the old image. */
    char comm_src[64];
    const char *comm_path = kpath;
    if (argv_user) {
        uint64_t uarg0 = 0;
        int rc_comm = -EFAULT;
        if (copyin(&uarg0, (const char *)argv_user, sizeof(uint64_t)) == 0
            && uarg0) {
            rc_comm = copyin_cstr((const char *)(uintptr_t)uarg0,
                                  comm_src, sizeof(comm_src));
        }
        /* copyin_cstr returns -ENAMETOOLONG with a NUL-terminated truncated
         * buffer; accept that since p->comm is 15 chars anyway. */
        if ((rc_comm == 0 || rc_comm == -ENAMETOOLONG) && comm_src[0]) {
            comm_path = comm_src;
        }
    }

    // Resolve through VFS — works for any filesystem with a readpage op
    // (tarfs, sbfs, tmpfs, ...). Don't cast fs_data; let the loader pull
    // bytes through the inode's read path.
    struct inode *ip;
    {
        /* Propagate namei's -errno (-ENOENT / -ENOTDIR / -ELOOP /
         * -ENAMETOOLONG) so execve callers see the real failure. */
        int nrc = namei(kpath, &ip);
        if (nrc < 0) {
            printk("exec: '%s' lookup failed (%d)\n", kpath, nrc);
            return nrc;
        }
    }
    if (ip->type != I_REG) {
        inode_put(ip);
        return -EACCES;
    }

    pgtable_t new_pt = create_user_pgtable();
    if (!new_pt) {
        inode_put(ip);
        return -ENOMEM;
    }

    struct vma *vlist = 0;
    uint64_t brk = 0;
    unsigned long entry;
    int load_rc = load_user_elf(new_pt, ip, &entry, &vlist, &brk);
    inode_put(ip);
    if (load_rc < 0) {
        free_user_pgtable(new_pt);
        return load_rc;
    }
    void *kstack = map_stack(new_pt);
    if (!kstack) {
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -ENOMEM;
    }

    // Build user stack (copies argv strings from OLD address space before switch)
    unsigned long new_sp = setup_user_stack(kstack, argv_user);
    if (new_sp == 0) {
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -ENOMEM;
    }

    // Heap VMA (zero-length initially)
    struct vma *heap_vma = vma_alloc();
    if (!heap_vma) {
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -ENOMEM;
    }
    heap_vma->start = brk;
    heap_vma->end   = brk;
    heap_vma->prot  = VMA_PROT_R | VMA_PROT_W;
    heap_vma->type  = VMA_TYPE_HEAP;
    if (vma_insert(&vlist, heap_vma) < 0) {
        vma_free(heap_vma);
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -EINVAL;
    }

    // Stack VMA
    struct vma *stack_vma = vma_alloc();
    if (!stack_vma) {
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -ENOMEM;
    }
    stack_vma->start = USER_STACK_TOP - 4096;
    stack_vma->end   = USER_STACK_TOP;
    stack_vma->prot  = VMA_PROT_R | VMA_PROT_W;
    stack_vma->type  = VMA_TYPE_STACK;
    if (vma_insert(&vlist, stack_vma) < 0) {
        vma_free(stack_vma);
        vma_list_free(&vlist);
        free_user_pgtable(new_pt);
        return -EINVAL;
    }

    // Free old VMAs and page table
    for (struct vma *vv = p->vma_list; vv; vv = vv->next) {
        if (vv->type == VMA_TYPE_FILE)
            vma_drop_file_pages(p, vv);
    }
    vma_list_free(&p->vma_list);
    pgtable_t old_pt = p->pagetable;

    p->pagetable  = new_pt;
    p->user_entry = entry;
    p->user_sp    = new_sp;
    p->vma_list   = vlist;
    p->heap_vma   = heap_vma;
    p->brk_start  = brk;
    proc_set_comm_basename(p, comm_path);
    {
        int j;
        for (j = 0; kpath[j] && j < (int)sizeof(p->exe_path) - 1; j++)
            p->exe_path[j] = kpath[j];
        p->exe_path[j] = '\0';
    }

    trapframe[TF_SEPC] = entry;
    trapframe[1] = new_sp;   // x2 = sp
    write_satp(make_satp(new_pt));
    flush_tlb();
    free_user_pgtable(old_pt);

    /* Reset caught signals to SIG_DFL; preserve mask and pending. */
    for (int i = 1; i < NSIG; i++) {
        if (p->sig_handlers[i].sa_handler != SIG_IGN)
            p->sig_handlers[i].sa_handler = SIG_DFL;
        p->sig_handlers[i].sa_restorer = 0;
        p->sig_handlers[i].sa_mask     = 0;
        p->sig_handlers[i].sa_flags    = 0;
    }
    p->in_sighandler   = 0;
    p->delivering_segv = 0;
    p->alarm_tick      = 0;        /* POSIX: pending alarm cleared on exec */
    p->did_exec        = 1;

    return 0;
}

// ---------------------------------------------------------------------------
// sys_sbrk
// ---------------------------------------------------------------------------
static int64_t sys_sbrk(int64_t incr) {
    struct pcb *p = current_proc();
    if (!p || !p->heap_vma) return -EINVAL;

    uint64_t old_end = p->heap_vma->end;
    uint64_t new_end = old_end + (uint64_t)incr;

    if (incr > 0) {
        if (new_end < old_end) return -ENOMEM;  /* size_t overflow */
        /* Cap at the lowest VMA start above the current heap end so the
         * heap does not collide with libc's fixed arena, prior mmap'd
         * regions, or the user stack. With no VMA above, allow growth up
         * to MMAP_BASE (start of the dynamic mmap region). No artificial
         * fixed ceiling — limit is whatever the address space actually
         * has free. */
        uint64_t ceiling = MMAP_BASE;
        for (struct vma *v = p->vma_list; v; v = v->next) {
            if (v == p->heap_vma) continue;
            if (v->start >= old_end && v->start < ceiling)
                ceiling = v->start;
        }
        if (new_end > ceiling) return -ENOMEM;
        p->heap_vma->end = new_end;
    } else if (incr < 0) {
        if (new_end < p->heap_vma->start) return -EINVAL;
        uint64_t unmap_start = page_round_up(new_end);
        uint64_t unmap_end   = page_round_up(old_end);
        if (unmap_start < unmap_end) {
            uvmunmap_range(p->pagetable, unmap_start, unmap_end);
            flush_tlb();
        }
        p->heap_vma->end = new_end;
    }

    return (int64_t)old_end;
}

// ---------------------------------------------------------------------------
// sys_mmap
// ---------------------------------------------------------------------------
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01
#define MAP_FIXED   0x10
#define MAP_ANON    0x20

static int64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags,
                        int fd, uint64_t off) {
    if (len == 0) return -EINVAL;
    {
        uint64_t aligned = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (aligned < len) return -EINVAL;   /* page-rounding wrapped */
        len = aligned;
    }
    if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0) return -EINVAL;
    if ((flags & MAP_PRIVATE) && (flags & MAP_SHARED)) return -EINVAL;
    /* RISC-V reserves W-only PTE encoding; the fault handler installs
     * PTE_W only alongside PTE_R, so reject mmap requests that would
     * grant write without read. Aligns the syscall's permission contract
     * with what user_page_fault will actually program. */
    if ((prot & VMA_PROT_W) && !(prot & VMA_PROT_R)) return -EINVAL;

    struct inode *fip = 0;
    uint64_t      file_off = 0;

    if (!(flags & MAP_ANON)) {
        if (fd < 0) return -EINVAL;
        if (off & (PAGE_SIZE - 1)) return -EINVAL;
        struct pcb *pp = current_proc();
        if (!pp) return -EINVAL;
        if (fd >= NOFILE) return -EBADF;
        struct file *f = pp->ofile[fd];
        if (!f) return -EBADF;
        if (f->type != FD_INODE) return -EACCES;
        if (!f->ip || f->ip->type != I_REG) return -EACCES;
        if (!f->ip->ops || !f->ip->ops->readpage) return -ENODEV;
        if ((prot & VMA_PROT_W) && (flags & MAP_SHARED)) {
            if (!f->ip->ops->writepage) return -EROFS;
            if (!f->writable) return -EACCES;
        }
        if ((prot & VMA_PROT_R) && !f->readable) return -EACCES;
        fip = inode_get(f->ip);
        file_off = off;
    } else {
        if (fd != -1 || off != 0) return -EINVAL;
    }

    struct pcb *proc = current_proc();
    if (!proc) { if (fip) inode_put(fip); return -EINVAL; }

    uint64_t search;
    if (flags & MAP_FIXED) {
        if (addr == 0 || (addr & (PAGE_SIZE - 1))) {
            if (fip) inode_put(fip);
            return -EINVAL;
        }
        if (addr < USER_TEXT_BASE) {
            if (fip) inode_put(fip);
            return -EINVAL;
        }
        if (addr + len < addr) {       /* user-controlled overflow */
            if (fip) inode_put(fip);
            return -EINVAL;
        }
        if (addr + len > USER_STACK_TOP) {
            if (fip) inode_put(fip);
            return -EINVAL;
        }
        rlim_t stack_max = proc->rlim[RLIMIT_STACK].rlim_cur;
        uint64_t stack_cap = USER_STACK_TOP - USER_TEXT_BASE;
        if (stack_max == RLIM_INFINITY || stack_max > stack_cap)
            stack_max = stack_cap;
        if (addr + len > USER_STACK_TOP - stack_max) {
            if (fip) inode_put(fip);
            return -EINVAL;
        }
        for (struct vma *v = proc->vma_list; v; v = v->next) {
            if (addr < v->end && addr + len > v->start) {
                if (fip) inode_put(fip);
                return -EINVAL;
            }
        }
        search = addr;
    } else {
        if (len > MMAP_END - MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
        search = (MMAP_END - len) & ~(PAGE_SIZE - 1);
        while (search >= MMAP_START) {
            int overlap = 0;
            for (struct vma *v = proc->vma_list; v; v = v->next) {
                if (search < v->end && search + len > v->start) {
                    overlap = 1;
                    if (v->start < MMAP_START + len) {
                        if (fip) inode_put(fip);
                        return -ENOMEM;
                    }
                    search = (v->start - len) & ~(PAGE_SIZE - 1);
                    break;
                }
            }
            if (!overlap) break;
            if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
        }
        if (search < MMAP_START) { if (fip) inode_put(fip); return -ENOMEM; }
    }

    struct vma *v = vma_alloc();
    if (!v) { if (fip) inode_put(fip); return -ENOMEM; }
    v->start    = search;
    v->end      = search + len;
    v->prot     = (uint32_t)prot;
    v->flags    = 0;
    v->file     = 0;
    v->file_off = 0;
    if (fip) {
        v->type     = VMA_TYPE_FILE;
        v->file     = fip;
        v->file_off = file_off;
        if (flags & MAP_SHARED) v->flags |= VMA_FLAG_SHARED;
        if ((flags & MAP_PRIVATE) && (prot & VMA_PROT_W)) {
            v->flags |= VMA_FLAG_COW;
        }
    } else {
        v->type = VMA_TYPE_ANON;
    }
    if (vma_insert(&proc->vma_list, v) < 0) {
        if (fip) inode_put(fip);
        vma_free(v);
        return -EINVAL;
    }
    return (int64_t)search;
}

// ---------------------------------------------------------------------------
// sys_munmap
// ---------------------------------------------------------------------------
static int64_t sys_munmap(uint64_t addr, uint64_t len) {
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    if (addr & (PAGE_SIZE - 1)) return -EINVAL;
    if (len == 0) return -EINVAL;
    len = page_round_up(len);

    struct vma *v = vma_find(p->vma_list, addr);
    if (!v) return -EINVAL;
    if (addr + len > v->end) return -EINVAL;

    if (v->type == VMA_TYPE_FILE) {
        /* Partial unmap of a file VMA is unsafe: uvmunmap_range below
         * would page_put() pcache slot pages (ref=1 from pcache_init),
         * dropping their refs to zero and returning them to the page
         * allocator while pcache still owns the slot. vma_split also
         * does not propagate file/file_off into a middle-cut right
         * half, leaving a null-file VMA that would deref on next
         * fault. Until a range-aware file teardown lands (Phase D),
         * reject anything that isn't a whole-VMA unmap. */
        int whole_vma = (addr <= v->start && addr + len >= v->end);
        if (!whole_vma) return -EINVAL;
        vma_drop_file_pages(p, v);
    }
    uvmunmap_range(p->pagetable, addr, addr + len);
    vma_split(&p->vma_list, v, addr, addr + len);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_msync
// ---------------------------------------------------------------------------
#define MS_SYNC 0x4

static int64_t sys_msync(uint64_t addr, uint64_t len, int flags) {
    if (flags != MS_SYNC) return -EINVAL;
    if (len == 0) return 0;
    if (addr & (PAGE_SIZE - 1)) return -EINVAL;
    uint64_t end = addr + len;
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    for (struct vma *v = p->vma_list; v; v = v->next) {
        if (v->type != VMA_TYPE_FILE) continue;
        if (!(v->flags & VMA_FLAG_SHARED)) continue;
        uint64_t s = (addr > v->start) ? addr : v->start;
        uint64_t e = (end < v->end) ? end : v->end;
        if (s >= e) continue;
        for (uint64_t va = s; va < e; va += PAGE_SIZE) {
            pte_t *pte = get_pte(p->pagetable, va, 0);
            if (!pte || !(*pte & PTE_V)) continue;
            uint64_t pgidx =
                (va - v->start + v->file_off) / PAGE_SIZE;
            struct pcache_page *pp;
            if (pcache_get(v->file, pgidx, &pp) < 0) continue;
            if (pp->dirty && v->file->ops &&
                v->file->ops->writepage_locked) {
                v->file->ops->writepage_locked(v->file, pgidx, pp->page);
                pp->dirty = 0;
            }
            pcache_put(pp);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// sys_getrlimit / sys_setrlimit (POSIX)
// ---------------------------------------------------------------------------
static int64_t sys_getrlimit(int resource, struct rlimit *urlim) {
    if (resource < 0 || resource >= RLIMITS_NR) return -EINVAL;
    if (!urlim) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    struct rlimit r = p->rlim[resource];
    if (copyout(urlim, &r, sizeof(r)) < 0) return -EFAULT;
    return 0;
}

static int64_t sys_setrlimit(int resource, const struct rlimit *urlim) {
    if (resource < 0 || resource >= RLIMITS_NR) return -EINVAL;
    if (!urlim) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EINVAL;
    struct rlimit r;
    if (copyin(&r, urlim, sizeof(r)) < 0) return -EFAULT;
    /* RLIM_INFINITY is (rlim_t)-1 = max unsigned, so a straight unsigned
     * compare correctly treats it as larger than any finite value: a
     * finite cur with infinite max is fine; an infinite cur with finite
     * max is rejected. */
    if (r.rlim_cur > r.rlim_max) return -EINVAL;
    /* Single-user OS: allow raising rlim_max without privilege check. */
    p->rlim[resource] = r;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_pipe
// ---------------------------------------------------------------------------
static int64_t sys_pipe(int *fds) {
    if (!fds) return -EFAULT;
    struct pcb *p = current_proc();
    if (!p) return -EBADF;
    if (proc_open_fd_count(p) + 2 > proc_fd_limit(p))
        return -EMFILE;

    struct file *rf = 0, *wf = 0;
    if (pipe_alloc(&rf, &wf) < 0) return -ENOMEM;
    int fd0 = alloc_fd(p, rf);
    if (fd0 < 0) { fileclose(rf); fileclose(wf); return -EMFILE; }
    int fd1 = alloc_fd(p, wf);
    if (fd1 < 0) { p->ofile[fd0] = 0; fileclose(rf); fileclose(wf); return -EMFILE; }
    int out[2] = {fd0, fd1};
    if (copyout(fds, out, sizeof(out)) < 0) {
        p->ofile[fd0] = 0;
        p->ofile[fd1] = 0;
        fileclose(rf);
        fileclose(wf);
        return -EFAULT;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// sys_fork
// ---------------------------------------------------------------------------
static int64_t sys_fork(void) {
    return (int64_t)proc_fork_current();
}

// ---------------------------------------------------------------------------
// sys_getppid
// ---------------------------------------------------------------------------
static int64_t sys_getppid(void) {
    struct pcb *p = current_proc();
    return p ? (int64_t)p->parent_pid : -1;
}

// ---------------------------------------------------------------------------
// sys_yield
// ---------------------------------------------------------------------------
static int64_t sys_yield(void) {
    yield();
    return 0;
}

// ---------------------------------------------------------------------------
// sys_sleep
// ---------------------------------------------------------------------------
static int64_t sys_sleep(uint64_t ms) {
    proc_sleep_ms(ms);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_clock_gettime
// ---------------------------------------------------------------------------
static int64_t sys_clock_gettime(int clockid, struct timespec *ts) {
    if (!ts) return -EFAULT;
    struct timespec kts;
    if (clockid == CLOCK_REALTIME) {
        uint64_t ns = rtc_read_ns();
        kts.tv_sec  = (int64_t)(ns / 1000000000UL);
        kts.tv_nsec = (int64_t)(ns % 1000000000UL);
    } else if (clockid == CLOCK_MONOTONIC) {
        uint64_t ticks = timer_ticks();
        kts.tv_sec  = (int64_t)(ticks / TICKS_PER_SEC);
        kts.tv_nsec = (int64_t)((ticks % TICKS_PER_SEC) * (1000000000UL / TICKS_PER_SEC));
    } else {
        return -EINVAL;
    }
    if (copyout(ts, &kts, sizeof(kts)) < 0) return -EFAULT;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_gettimeofday
// ---------------------------------------------------------------------------
static int64_t sys_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -EFAULT;
    uint64_t ns = rtc_read_ns();
    struct timeval ktv = {
        .tv_sec  = (int64_t)(ns / 1000000000UL),
        .tv_usec = (int64_t)((ns % 1000000000UL) / 1000UL),
    };
    if (copyout(tv, &ktv, sizeof(ktv)) < 0) return -EFAULT;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_nanosleep — basic version; -EINTR added in Phase 8b once signals land
// ---------------------------------------------------------------------------
static int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem) {
    struct timespec kreq;
    if (copyin(&kreq, req, sizeof(kreq)) < 0) return -EFAULT;
    if (kreq.tv_sec < 0 || kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000LL)
        return -EINVAL;

    struct pcb *p = current_proc();
    if (!p) return -EINVAL;

    uint64_t total_ticks = (uint64_t)kreq.tv_sec * (uint64_t)TICKS_PER_SEC
                         + ((uint64_t)kreq.tv_nsec * (uint64_t)TICKS_PER_SEC) / 1000000000ULL;
    if (total_ticks == 0 && kreq.tv_nsec > 0) total_ticks = 1;

    uint64_t start = timer_ticks();
    uint64_t wake  = start + total_ticks;

    while (timer_ticks() < wake) {
        p->wake_tick = wake;
        proc_sleep(p);
        if (sig_has_actionable(p)) {
            uint64_t now = timer_ticks();
            uint64_t rem_ticks = (now >= wake) ? 0 : (wake - now);
            if (rem) {
                struct timespec krem;
                krem.tv_sec  = (int64_t)(rem_ticks / TICKS_PER_SEC);
                krem.tv_nsec = (int64_t)((rem_ticks % TICKS_PER_SEC) * (1000000000ULL / TICKS_PER_SEC));
                if (copyout(rem, &krem, sizeof(krem)) < 0) {
                    p->wake_tick = 0;
                    return -EFAULT;
                }
            }
            p->wake_tick = 0;
            return -EINTR;
        }
    }

    p->wake_tick = 0;
    if (rem) {
        struct timespec z = {0, 0};
        if (copyout(rem, &z, sizeof(z)) < 0) return -EFAULT;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Job control: process group / session syscalls
// ---------------------------------------------------------------------------
static struct pcb *pcb_target(int pid) {
    if (pid == 0) return current_proc();
    return proc_find_by_pid(pid);
}

static int64_t sys_setpgid(int pid, int pgid) {
    struct pcb *me = current_proc();
    if (!me) return -EINVAL;
    if (pid < 0 || pgid < 0) return -EINVAL;

    struct pcb *p = pcb_target(pid);
    if (!p) return -ESRCH;

    /* POSIX: caller may set its own pgid, or the pgid of a child that
     * has not yet called execve.  Without the parent path, a shell
     * that calls setpgid(child, child) before the child has had a
     * chance to self-setpgid loses the race and waitpid(-pgid)
     * returns -ECHILD immediately. */
    int is_self  = (p == me);
    int is_child = (p->parent_pid == me->pid);
    if (!is_self && !is_child) return -EPERM;
    if (is_child && p->did_exec) return -EACCES;

    if (p->sid != me->sid) return -EPERM;
    if (p->pid == p->sid) return -EPERM;          // session leader

    if (pgid == 0) pgid = p->pid;
    /* Target pgrp must exist within the caller's session, unless the
     * caller is making itself into the new pgrp's leader. */
    if (pgid != p->pid) {
        struct pcb *leader = proc_find_by_pid(pgid);
        if (!leader || leader->sid != me->sid) return -EPERM;
    }
    p->pgid = pgid;
    return 0;
}

static int64_t sys_getpgid(int pid) {
    struct pcb *p = pcb_target(pid);
    if (!p) return -ESRCH;
    return p->pgid;
}

static int64_t sys_getpgrp(void) {
    return current_proc()->pgid;
}

static int64_t sys_getsid(int pid) {
    struct pcb *p = pcb_target(pid);
    if (!p) return -ESRCH;
    return p->sid;
}

static int64_t sys_setsid(void) {
    struct pcb *me = current_proc();
    /* Cannot setsid if already a process-group leader of any other proc. */
    for (struct pcb *q = proc_list_head(); q; q = q->next) {
        if (q == me) continue;
        if (q->state == PROC_UNUSED) continue;
        if (q->pgid == me->pid) return -EPERM;
    }
    me->sid  = me->pid;
    me->pgid = me->pid;
    return me->sid;
}

// ---------------------------------------------------------------------------
// uid/gid stubs — always 0, never fail
// ---------------------------------------------------------------------------
static int64_t sys_getuid(void)  { return 0; }
static int64_t sys_geteuid(void) { return 0; }
static int64_t sys_getgid(void)  { return 0; }
static int64_t sys_getegid(void) { return 0; }
static int64_t sys_setuid(int uid)  { (void)uid; return 0; }
static int64_t sys_setgid(int gid)  { (void)gid; return 0; }

// ---------------------------------------------------------------------------
// sys_ioctl
// ---------------------------------------------------------------------------
static int64_t sys_ioctl(int fd, int cmd, unsigned long arg) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    return fileioctl(p->ofile[fd], cmd, arg);
}

static int64_t sys_meminfo(void) {
    return (int64_t)pmem_free_count();
}

// ---------------------------------------------------------------------------
// sys_mount(target, fstype) — userspace mount entry point.
//
// Supported fstypes: "proc" (procfs), "disk" (sbfs), "tmpfs".
// The CLI form is `mount -t TYPE [SOURCE] TARGET`; SOURCE is ignored
// (we have no /dev fs), so only target + fstype reach the kernel.
// Returns 0 on success, negative errno otherwise.
// ---------------------------------------------------------------------------
static int64_t sys_mount(const char *u_target, const char *u_fstype) {
    char target[PATH_MAX_LOCAL];
    char fstype[16];
    int rc = copyin_cstr(u_target, target, sizeof(target));
    if (rc < 0) return rc;
    rc = copyin_cstr(u_fstype, fstype, sizeof(fstype));
    if (rc < 0) return rc;

    if (strcmp(fstype, "proc") == 0)
        return procfs_attach(target);
    if (strcmp(fstype, "disk") == 0)
        return sbfs_attach(target);
    if (strcmp(fstype, "tmpfs") == 0)
        return tmpfs_attach(target);
    return -EINVAL;
}

// ---------------------------------------------------------------------------
// sys_truncate / sys_ftruncate — set a file's length.
//
// Currently only `length == 0` is supported (full truncate); any other
// length returns -EINVAL because the underlying fs ops only know how to
// drop all data blocks. Extending or partial-shrink would need a richer
// truncate-to(ip, length) op which is left for a future change.
// ---------------------------------------------------------------------------
static int64_t do_truncate_inode(struct inode *ip, int64_t length) {
    if (ip->type != I_REG) return -EINVAL;
    if (length < 0) return -EINVAL;
    if (length == (int64_t)ip->size) return 0;          /* no-op */
    if (length != 0) return -EINVAL;                    /* see comment */
    if (!ip->ops || !ip->ops->truncate) return -EROFS;
    return ip->ops->truncate(ip);
}

static int64_t sys_truncate(const char *u_path, int64_t length) {
    char path[PATH_MAX_LOCAL];
    int rc = copyin_cstr(u_path, path, sizeof(path));
    if (rc < 0) return rc;
    struct inode *ip;
    /* namei already returns -errno (e.g. -ENOENT, -ENOTDIR, -EACCES,
     * -ELOOP). Propagate it so userspace sees the real failure reason
     * instead of an over-coarse -ENOENT. */
    int nrc = namei(path, &ip);
    if (nrc < 0) return nrc;
    rc = (int)do_truncate_inode(ip, length);
    inode_put(ip);
    return rc;
}

static int64_t sys_ftruncate(int fd, int64_t length) {
    struct pcb *p = current_proc();
    if (!p || fd < 0 || fd >= NOFILE || !p->ofile[fd]) return -EBADF;
    struct file *f = p->ofile[fd];
    if (!f->writable) return -EBADF;
    if (!f->ip) return -EINVAL;
    return do_truncate_inode(f->ip, length);
}

// ---------------------------------------------------------------------------
// sys_alarm(secs) — schedule a SIGALRM after `secs` seconds.
//
// Replaces any prior pending alarm; returns the prior alarm's remaining
// seconds (rounded up), or 0 if none was set. secs == 0 cancels.
// ---------------------------------------------------------------------------
static int64_t sys_alarm(unsigned secs) {
    struct pcb *p = current_proc();
    if (!p) return 0;

    uint64_t now = timer_ticks();
    uint64_t prior_remaining = 0;
    if (p->alarm_tick != 0 && p->alarm_tick > now) {
        uint64_t ticks_left = p->alarm_tick - now;
        /* round up to whole seconds */
        prior_remaining = (ticks_left + TICKS_PER_SEC - 1) / TICKS_PER_SEC;
    }

    if (secs == 0) {
        p->alarm_tick = 0;
    } else {
        p->alarm_tick = now + (uint64_t)secs * TICKS_PER_SEC;
        if (p->alarm_tick == 0) p->alarm_tick = 1;  /* never use sentinel */
    }
    return (int64_t)prior_remaining;
}
// ---------------------------------------------------------------------------
// syscall_dispatch
// ---------------------------------------------------------------------------
int64_t syscall_dispatch(uint64_t sysnum, uint64_t *trapframe) {
    switch (sysnum) {
        case SYS_exit:
            return sys_exit((int)(int64_t)trapframe[TF_A0]);

        case SYS_write:
            return sys_write((int)(int64_t)trapframe[TF_A0],
                             (const char *)trapframe[TF_A1],
                             trapframe[TF_A2]);

        case SYS_read:
            return sys_read((int)(int64_t)trapframe[TF_A0],
                            (void *)trapframe[TF_A1],
                            trapframe[TF_A2]);

        case SYS_open:
            return sys_open((const char *)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);

        case SYS_close:
            return sys_close((int)(int64_t)trapframe[TF_A0]);

        case SYS_getpid:
            return sys_getpid();

        case SYS_exec:
            return do_exec((const char *)trapframe[TF_A0], 0, trapframe);

        case SYS_execv:
            return do_exec((const char *)trapframe[TF_A0],
                           (char *const *)trapframe[TF_A1], trapframe);

        case SYS_fork:
            return sys_fork();

        case SYS_wait: {
            int *ustatus = (int *)(uintptr_t)trapframe[TF_A0];
            int kstatus = 0;
            int r = proc_wait_current(ustatus ? &kstatus : 0);
            if (r > 0 && ustatus) {
                if (copyout(ustatus, &kstatus, sizeof(kstatus)) < 0)
                    return -EFAULT;
            }
            return (int64_t)r;
        }

        case SYS_getppid:
            return sys_getppid();

        case SYS_yield:
            return sys_yield();

        case SYS_sleep:
            return sys_sleep(trapframe[TF_A0]);

        case SYS_dup:
            return sys_dup((int)(int64_t)trapframe[TF_A0]);

        case SYS_dup2:
            return sys_dup2((int)(int64_t)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);

        case SYS_lseek:
            return sys_lseek((int)(int64_t)trapframe[TF_A0],
                             (int64_t)trapframe[TF_A1],
                             (int)(int64_t)trapframe[TF_A2]);

        case SYS_fstat:
            return sys_fstat((int)(int64_t)trapframe[TF_A0],
                             (struct stat *)trapframe[TF_A1]);

        case SYS_readlink:
            return sys_readlink((const char *)trapframe[TF_A0],
                                (char *)trapframe[TF_A1],
                                trapframe[TF_A2]);

        case SYS_lstat:
            return sys_lstat((const char *)trapframe[TF_A0],
                             (struct stat *)trapframe[TF_A1]);

        case SYS_getdents64:
            return sys_getdents64((int)(int64_t)trapframe[TF_A0],
                                  (void *)trapframe[TF_A1],
                                  trapframe[TF_A2]);

        case SYS_chdir:
            return sys_chdir((const char *)trapframe[TF_A0]);

        case SYS_getcwd:
            return sys_getcwd((char *)trapframe[TF_A0], trapframe[TF_A1]);

        case SYS_mkdir:
            return sys_mkdir((const char *)trapframe[TF_A0]);

        case SYS_unlink:
            return sys_unlink((const char *)trapframe[TF_A0]);

        case SYS_link:
            return sys_link((const char *)trapframe[TF_A0],
                            (const char *)trapframe[TF_A1]);

        case SYS_rename:
            return sys_rename((const char *)trapframe[TF_A0],
                              (const char *)trapframe[TF_A1]);

        case SYS_pipe:
            return sys_pipe((int *)trapframe[TF_A0]);

        case SYS_sbrk:
            return sys_sbrk((int64_t)trapframe[TF_A0]);

        case SYS_mmap:
            return sys_mmap(trapframe[TF_A0], trapframe[TF_A1],
                            (int)(int64_t)trapframe[TF_A2],
                            (int)(int64_t)trapframe[TF_A3],
                            (int)(int64_t)trapframe[TF_A4],
                            (uint64_t)trapframe[TF_A5]);

        case SYS_munmap:
            return sys_munmap(trapframe[TF_A0], trapframe[TF_A1]);

        case SYS_msync:
            return sys_msync(trapframe[TF_A0], trapframe[TF_A1],
                             (int)trapframe[TF_A2]);

        case SYS_getrlimit:
            return sys_getrlimit((int)trapframe[TF_A0],
                                 (struct rlimit *)trapframe[TF_A1]);
        case SYS_setrlimit:
            return sys_setrlimit((int)trapframe[TF_A0],
                                 (const struct rlimit *)trapframe[TF_A1]);

        case SYS_clock_gettime:
            return sys_clock_gettime((int)(int64_t)trapframe[TF_A0],
                                     (struct timespec *)trapframe[TF_A1]);

        case SYS_gettimeofday:
            return sys_gettimeofday((struct timeval *)trapframe[TF_A0],
                                    (void *)trapframe[TF_A1]);

        case SYS_nanosleep:
            return sys_nanosleep((const struct timespec *)trapframe[TF_A0],
                                 (struct timespec *)trapframe[TF_A1]);

        case SYS_kill:
            return sys_kill((int)(int64_t)trapframe[TF_A0],
                            (int)(int64_t)trapframe[TF_A1]);

        case SYS_sigaction:
            return sys_sigaction((int)(int64_t)trapframe[TF_A0],
                                 (const struct sigaction *)trapframe[TF_A1],
                                 (struct sigaction *)trapframe[TF_A2]);

        case SYS_sigprocmask:
            return sys_sigprocmask((int)(int64_t)trapframe[TF_A0],
                                   (const sigset_t *)trapframe[TF_A1],
                                   (sigset_t *)trapframe[TF_A2]);

        case SYS_sigreturn:
            return sys_sigreturn(trapframe);

        case SYS_pause:
            return sys_pause();

        case SYS_sigsuspend:
            return sys_sigsuspend((const sigset_t *)(uintptr_t)trapframe[TF_A0]);

        case SYS_getuid:  return sys_getuid();
        case SYS_geteuid: return sys_geteuid();
        case SYS_getgid:  return sys_getgid();
        case SYS_getegid: return sys_getegid();
        case SYS_setuid:  return sys_setuid((int)(int64_t)trapframe[TF_A0]);
        case SYS_setgid:  return sys_setgid((int)(int64_t)trapframe[TF_A0]);

        case SYS_ioctl:
            return sys_ioctl((int)(int64_t)trapframe[TF_A0],
                             (int)(int64_t)trapframe[TF_A1],
                             (unsigned long)trapframe[TF_A2]);

        case SYS_meminfo:
            return sys_meminfo();

        case SYS_mount:
            return sys_mount((const char *)trapframe[TF_A0],
                             (const char *)trapframe[TF_A1]);

        case SYS_alarm:
            return sys_alarm((unsigned)trapframe[TF_A0]);

        case SYS_truncate:
            return sys_truncate((const char *)trapframe[TF_A0],
                                (int64_t)trapframe[TF_A1]);

        case SYS_ftruncate:
            return sys_ftruncate((int)(int64_t)trapframe[TF_A0],
                                 (int64_t)trapframe[TF_A1]);

        case SYS_symlink:
            return sys_symlink((const char *)trapframe[TF_A0],
                               (const char *)trapframe[TF_A1]);

        case SYS_wait4: {
            int pid_a       = (int)(int64_t)trapframe[TF_A0];
            int *ustatus    = (int *)(uintptr_t)trapframe[TF_A1];
            int options     = (int)(int64_t)trapframe[TF_A2];
            int kstatus = 0;
            int r = proc_wait4_current(pid_a, ustatus ? &kstatus : 0, options);
            if (r > 0 && ustatus) {
                if (copyout(ustatus, &kstatus, sizeof(kstatus)) < 0)
                    return -EFAULT;
            }
            return (int64_t)r;
        }

        case SYS_setpgid:
            return sys_setpgid((int)(int64_t)trapframe[TF_A0],
                               (int)(int64_t)trapframe[TF_A1]);
        case SYS_getpgid:
            return sys_getpgid((int)(int64_t)trapframe[TF_A0]);
        case SYS_getpgrp:
            return sys_getpgrp();
        case SYS_setsid:
            return sys_setsid();
        case SYS_getsid:
            return sys_getsid((int)(int64_t)trapframe[TF_A0]);

        default:
            printk("syscall: unknown number %lu from pid %d\n",
                   sysnum,
                   current_proc() ? current_proc()->pid : -1);
            return -ENOSYS;
    }
}
