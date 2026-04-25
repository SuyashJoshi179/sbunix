#include <errno.h>
#include <exec.h>
#include <file.h>
#include <inode.h>
#include <pipe.h>
#include <pmem.h>
#include <printk.h>
#include <proc.h>
#include <riscv.h>
#include <sbfs.h>
#include <signal.h>
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
    if (!p) return 0;
    int lim = p->rlim_nofile;
    if (lim < 0) lim = 0;
    if (lim > NOFILE) lim = NOFILE;
    return lim;
}

static int proc_open_fd_count(const struct pcb *p) {
    int n = 0;
    if (!p) return 0;
    for (int fd = 0; fd < NOFILE; fd++)
        if (p->ofile[fd]) n++;
    return n;
}

static int proc_vma_count(const struct pcb *p) {
    int n = 0;
    if (!p) return 0;
    for (struct vma *v = p->vma_list; v; v = v->next)
        n++;
    return n;
}

static uint64_t proc_vma_total_pages(const struct pcb *p) {
    uint64_t pages = 0;
    if (!p) return 0;
    for (struct vma *v = p->vma_list; v; v = v->next) {
        if (v->end > v->start)
            pages += (v->end - v->start) / PAGE_SIZE;
    }
    return pages;
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
    proc_exit_current(status);
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
// path_split — split an absolute path into parent dir path + leaf name.
// parent_buf must hold at least the length of path.
// Returns 0 on success, -EINVAL if path has no parent component.
// ---------------------------------------------------------------------------
static int path_split(const char *path, char *parent_buf, const char **leaf_out) {
    int len = 0;
    while (path[len]) len++;
    if (len == 0 || path[0] != '/') return -EINVAL;

    // Find last '/' (excluding a trailing slash).
    int last_slash = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }
    if (last_slash < 0) return -EINVAL;

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
        if (namei(parent_path, &parent) < 0) return -ENOENT;
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
    if (namei(parent_path, &parent) < 0) return -ENOENT;
    if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }
    if (!parent->ops || !parent->ops->mkdir) { inode_put(parent); return -EROFS; }

    // Check name doesn't already exist
    if (parent->ops->lookup) {
        struct inode *existing = 0;
        if (parent->ops->lookup(parent, leaf, &existing) == 0) {
            inode_put(existing);
            inode_put(parent);
            return -EEXIST;
        }
    }

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
    if (namei(parent_path, &parent) < 0) return -ENOENT;
    if (parent->type != I_DIR) { inode_put(parent); return -ENOTDIR; }
    if (!parent->ops || !parent->ops->unlink) { inode_put(parent); return -EROFS; }

    int rc = parent->ops->unlink(parent, leaf);
    inode_put(parent);
    return rc;
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
    int rc = filestat(p->ofile[fd], &kst);
    if (rc < 0) return rc;
    if (copyout(st, &kst, (unsigned long)sizeof(kst)) < 0) return -EFAULT;
    return rc;
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
    if (!p || !p->is_user) return -1;
    char kpath[PATH_MAX_LOCAL];
    int rc_path = copyin_cstr(path, kpath, sizeof(kpath));
    if (rc_path < 0) return rc_path;

    // Resolve through VFS.
    struct inode *ip;
    if (namei(kpath, &ip) < 0) {
        printk("exec: '%s' not found\n", kpath);
        return -ENOENT;
    }
    unsigned long img_sz = ip->size;
    struct { const char *data; unsigned long file_size; } *td = ip->fs_data;
    const void *img = td->data;
    inode_put(ip);

    if (!img) return -ENOENT;

    pgtable_t new_pt = create_user_pgtable();
    if (!new_pt) return -ENOMEM;

    struct vma *vlist = 0;
    uint64_t brk = 0;
    unsigned long entry;
    int load_rc = load_user_elf(new_pt, img, img_sz, &entry, &vlist, &brk);
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
    vma_insert(&vlist, heap_vma);

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
    vma_insert(&vlist, stack_vma);

    // Free old VMAs and page table
    vma_list_free(&p->vma_list);
    pgtable_t old_pt = p->pagetable;

    p->pagetable  = new_pt;
    p->user_entry = entry;
    p->user_sp    = new_sp;
    p->vma_list   = vlist;
    p->heap_vma   = heap_vma;
    p->brk_start  = brk;

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

    printk("exec: '%s' loaded, entry=0x%lx sp=0x%lx\n", kpath, entry, new_sp);
    return 0;
}

// ---------------------------------------------------------------------------
// sys_sbrk
// ---------------------------------------------------------------------------
static int64_t sys_sbrk(int64_t incr) {
    struct pcb *p = current_proc();
    if (!p || !p->heap_vma) return -1;

    uint64_t old_end = p->heap_vma->end;
    uint64_t new_end = old_end + (uint64_t)incr;

    if (incr > 0) {
        if (new_end > HEAP_MAX) return -ENOMEM;

        // Enforce per-process mapped-page cap against the VMA address range;
        // actual physical pages come on demand via user_page_fault.
        uint64_t old_pages = (old_end - p->heap_vma->start) / PAGE_SIZE;
        uint64_t new_pages = (new_end - p->heap_vma->start) / PAGE_SIZE;
        uint64_t add_pages = (new_pages > old_pages) ? (new_pages - old_pages) : 0;
        if (proc_vma_total_pages(p) + add_pages > (uint64_t)p->rlim_npages)
            return -ENOMEM;

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
#define MAP_ANON    0x20
#define PROT_READ   0x1
#define PROT_WRITE  0x2

static int64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags,
                        int fd, int64_t off) {
    (void)addr;
    struct pcb *p = current_proc();
    if (!p) return -1;
    if (len == 0) return -EINVAL;
    if (!(flags & MAP_ANON) || fd != -1 || off != 0) return -EINVAL;

    len = page_round_up(len);
    if (proc_vma_count(p) >= p->rlim_nvma) return -ENOMEM;
    if (proc_vma_total_pages(p) + (len / PAGE_SIZE) > (uint64_t)p->rlim_npages)
        return -ENOMEM;

    uint64_t search = MMAP_END - len;
    while (search >= MMAP_START) {
        int overlap = 0;
        for (struct vma *v = p->vma_list; v; v = v->next) {
            if (v->start < search + len && v->end > search) {
                if (v->start < MMAP_START) { overlap = 1; break; }
                search = (v->start >= len) ? v->start - len : 0;
                overlap = 1;
                break;
            }
        }
        if (!overlap) break;
        if (search < MMAP_START) return -ENOMEM;
    }
    if (search < MMAP_START) return -ENOMEM;

    struct vma *v = vma_alloc();
    if (!v) return -ENOMEM;
    v->start = search;
    v->end   = search + len;
    v->prot  = 0;
    if (prot & PROT_READ)  v->prot |= VMA_PROT_R;
    if (prot & PROT_WRITE) v->prot |= VMA_PROT_W;
    v->type = VMA_TYPE_ANON;
    vma_insert(&p->vma_list, v);

    return (int64_t)search;
}

// ---------------------------------------------------------------------------
// sys_munmap
// ---------------------------------------------------------------------------
static int64_t sys_munmap(uint64_t addr, uint64_t len) {
    struct pcb *p = current_proc();
    if (!p) return -1;
    if (addr & (PAGE_SIZE - 1)) return -EINVAL;
    if (len == 0) return -EINVAL;
    len = page_round_up(len);

    struct vma *v = vma_find(p->vma_list, addr);
    if (!v) return -EINVAL;
    if (addr + len > v->end) return -EINVAL;

    uvmunmap_range(p->pagetable, addr, addr + len);
    vma_split(&p->vma_list, v, addr, addr + len);
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
    if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) return -EINVAL;
    struct timespec kts;
    uint64_t ticks = timer_ticks();
    kts.tv_sec  = (int64_t)(ticks / TICKS_PER_SEC);
    kts.tv_nsec = (int64_t)((ticks % TICKS_PER_SEC) * (1000000000UL / TICKS_PER_SEC));
    if (copyout(ts, &kts, sizeof(kts)) < 0) return -EFAULT;
    return 0;
}

// ---------------------------------------------------------------------------
// sys_gettimeofday
// ---------------------------------------------------------------------------
static int64_t sys_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -EFAULT;
    struct timeval ktv;
    uint64_t ticks = timer_ticks();
    ktv.tv_sec  = (int64_t)(ticks / TICKS_PER_SEC);
    ktv.tv_usec = (int64_t)((ticks % TICKS_PER_SEC) * (1000000UL / TICKS_PER_SEC));
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

        case SYS_pipe:
            return sys_pipe((int *)trapframe[TF_A0]);

        case SYS_sbrk:
            return sys_sbrk((int64_t)trapframe[TF_A0]);

        case SYS_mmap:
            return sys_mmap(trapframe[TF_A0], trapframe[TF_A1],
                            (int)(int64_t)trapframe[TF_A2],
                            (int)(int64_t)trapframe[TF_A3],
                            (int)(int64_t)trapframe[TF_A4],
                            (int64_t)trapframe[TF_A5]);

        case SYS_munmap:
            return sys_munmap(trapframe[TF_A0], trapframe[TF_A1]);

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
        default:
            printk("syscall: unknown number %lu from pid %d\n",
                   sysnum,
                   current_proc() ? current_proc()->pid : -1);
            return -ENOSYS;
    }
}
