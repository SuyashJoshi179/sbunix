#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <vfs.h>
#include <vmem.h>
#include <timer.h>
#include <pmem.h>
#include <string.h>
#include <printk.h>
#include <procfs.h>
#include <proc.h>
#include <riscv.h>
#include <vma.h>

enum proc_kind {
    PK_PIDDIR = 1,
    PK_STATUS,
    PK_CMDLINE,
    PK_STAT,
    PK_STATM,
    PK_SELF,
    PK_CWD,
    PK_EXE,
    PK_ROOT,
};

struct proc_node {
    struct inode    ino;
    enum proc_kind  kind;
    int             pid;
    uint64_t        generation;
    int             in_use;
};

#define PROC_INODE_POOL 64
static struct proc_node pool[PROC_INODE_POOL];

static uint64_t procfs_irq_save(void) {
    uint64_t sstatus;

    __asm__ volatile("csrr %0, sstatus" : "=r"(sstatus));
    __asm__ volatile("csrc sstatus, %0" :: "r"(SSTATUS_SIE) : "memory");
    return sstatus;
}

static void procfs_irq_restore(uint64_t sstatus) {
    if (sstatus & SSTATUS_SIE)
        __asm__ volatile("csrs sstatus, %0" :: "r"(SSTATUS_SIE) : "memory");
}

static struct proc_node *pool_alloc(void) {
    uint64_t sstatus = procfs_irq_save();

    for (int i = 0; i < PROC_INODE_POOL; i++) {
        if (!pool[i].in_use) {
            struct proc_node *pn = &pool[i];
            for (uint64_t b = 0; b < sizeof(*pn); b++) ((char *)pn)[b] = 0;
            pn->in_use = 1;
            procfs_irq_restore(sstatus);
            return pn;
        }
    }

    procfs_irq_restore(sstatus);
    return 0;
}

static void pool_free(struct proc_node *pn) {
    uint64_t sstatus = procfs_irq_save();
    pn->in_use = 0;
    procfs_irq_restore(sstatus);
}

static int piddir_read(struct inode *ip, uint64_t off, void *buf, uint64_t n);
static int piddir_lookup(struct inode *dir, const char *name,
                         struct inode **out);
static int piddir_getdents(struct inode *dir, uint64_t off, void *buf,
                            uint64_t n, uint64_t *out_next);
static int piddir_stat(struct inode *ip, struct stat *st);
static void piddir_release(struct inode *ip);
static int piddir_file_read(struct inode *ip, uint64_t off, void *buf,
                             uint64_t n);
static int piddir_file_stat(struct inode *ip, struct stat *st);
static void piddir_file_release(struct inode *ip);

static struct inode proc_root_inode;

/* Tiny formatter for procfs files. Each producer fills `out` with up to
 * `cap` bytes and returns the byte count. The caller's read() then
 * services off..off+n from this snapshot. */
typedef int (*proc_producer)(char *out, int cap);

static int produce_static(proc_producer prod, uint64_t off, void *buf,
                           uint64_t n) {
    char tmp[512];
    int  total = prod(tmp, (int)sizeof(tmp));
    if (off >= (uint64_t)total) return 0;
    int copy = total - (int)off;
    if ((int)n < copy) copy = (int)n;
    char *dst = (char *)buf;
    for (int i = 0; i < copy; i++) dst[i] = tmp[(int)off + i];
    return copy;
}

/* Minimal unsigned-decimal formatter -- places digits in `out`, returns count.
 * Caller ensures cap >= 21. */
static int u64_to_dec(char *out, int cap, uint64_t v) {
    char tmp[24];
    int  i = 0;
    if (!v) tmp[i++] = '0';
    else while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    if (i > cap) i = cap;
    for (int j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    return i;
}

#ifndef SBUNIX_VERSION
#define SBUNIX_VERSION "unknown"
#endif

static int prod_uptime(char *out, int cap) {
    uint64_t ticks = timer_ticks();
    uint64_t secs  = ticks / TICKS_PER_SEC;
    uint64_t cs    = ((ticks % TICKS_PER_SEC) * 100) / TICKS_PER_SEC;
    int n = 0;
    n += u64_to_dec(out + n, cap - n, secs);
    if (n < cap) out[n++] = '.';
    if (cs < 10 && n < cap) out[n++] = '0';
    n += u64_to_dec(out + n, cap - n, cs);
    if (n < cap) out[n++] = '\n';
    return n;
}

static int prod_meminfo(char *out, int cap) {
    static const char k1[] = "MemFree: ";
    static const char k2[] = " kB\n";
    uint64_t free_kb = (uint64_t)pmem_free_count() * (PAGE_SIZE / 1024UL);
    int n = 0;
    for (int i = 0; i < (int)sizeof(k1) - 1 && n < cap; i++) out[n++] = k1[i];
    n += u64_to_dec(out + n, cap - n, free_kb);
    for (int i = 0; i < (int)sizeof(k2) - 1 && n < cap; i++) out[n++] = k2[i];
    return n;
}

static int prod_version(char *out, int cap) {
    static const char s[] = "SBUnix " SBUNIX_VERSION " riscv64\n";
    int n = (int)sizeof(s) - 1;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = s[i];
    return n;
}

static int prod_cpuinfo(char *out, int cap) {
    static const char s[] = "processor: 0\nisa: rv64imafdc\n";
    int n = (int)sizeof(s) - 1;
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = s[i];
    return n;
}

static int append_str(char *out, int cap, int n, const char *s) {
    for (int i = 0; s[i] && n < cap; i++) out[n++] = s[i];
    return n;
}

static int append_u64(char *out, int cap, int n, uint64_t v) {
    if (n >= cap) return n;
    int wrote = u64_to_dec(out + n, cap - n, v);
    if (wrote > cap - n) wrote = cap - n;
    return n + wrote;
}

static char state_letter(proc_state_t state) {
    switch (state) {
    case PROC_READY:    return 'R';
    case PROC_RUNNING:  return 'R';
    case PROC_SLEEPING: return 'S';
    case PROC_ZOMBIE:   return 'Z';
    case PROC_UNUSED:   return 'X';
    default:            return '?';
    }
}

/* Snapshot of pcb fields formatted by the producers below. Taking a
 * snapshot under IRQ-off lets us format from stable values after
 * re-enabling interrupts, avoiding use-after-free if the pcb is
 * reaped mid-format. */
struct proc_snap {
    proc_state_t state;
    int          pid;
    int          parent_pid;
    int          pgid;
    int          sid;
    char         comm[16];
    uint64_t     vm_size_kb;
    uint64_t     vm_stk_kb;
    /* Page-granularity totals for /proc/<pid>/statm. */
    uint64_t     vm_size_pages;
    uint64_t     vm_text_pages;
    uint64_t     vm_data_pages;
};

static int prod_status(char *out, int cap, const struct proc_snap *s) {
    const char *cm = (s->comm[0]) ? s->comm : "proc";
    int n = 0;
    n = append_str(out, cap, n, "Name:\t");
    n = append_str(out, cap, n, cm);
    n = append_str(out, cap, n, "\nState:\t");
    if (n < cap) out[n++] = state_letter(s->state);
    n = append_str(out, cap, n, "\nPid:\t");
    n = append_u64(out, cap, n, (uint64_t)s->pid);
    n = append_str(out, cap, n, "\nPPid:\t");
    n = append_u64(out, cap, n, (uint64_t)s->parent_pid);
    n = append_str(out, cap, n, "\nPgid:\t");
    n = append_u64(out, cap, n, (uint64_t)s->pgid);
    n = append_str(out, cap, n, "\nSid:\t");
    n = append_u64(out, cap, n, (uint64_t)s->sid);
    n = append_str(out, cap, n, "\nUid:\t0\nGid:\t0\nVmSize:\t");
    n = append_u64(out, cap, n, s->vm_size_kb);
    n = append_str(out, cap, n, " kB\nVmStk:\t");
    n = append_u64(out, cap, n, s->vm_stk_kb);
    n = append_str(out, cap, n, " kB\n");
    return n;
}

static int prod_cmdline(char *out, int cap, const struct proc_snap *s) {
    const char *src = (s->comm[0]) ? s->comm : "proc";
    int n = 0;
    for (int i = 0; src[i] && n < cap; i++) out[n++] = src[i];
    /* Linux-style: each argv element is NUL-terminated. We only have argv[0]. */
    if (n < cap) out[n++] = '\0';
    return n;
}

static int prod_stat(char *out, int cap, const struct proc_snap *s) {
    const char *cm = (s->comm[0]) ? s->comm : "proc";
    int n = 0;
    n = append_u64(out, cap, n, (uint64_t)s->pid);
    n = append_str(out, cap, n, " (");
    n = append_str(out, cap, n, cm);
    n = append_str(out, cap, n, ") ");
    if (n < cap) out[n++] = state_letter(s->state);
    if (n < cap) out[n++] = ' ';
    n = append_u64(out, cap, n, (uint64_t)s->parent_pid);
    n = append_str(out, cap, n, " 0 0 0 0 0\n");
    return n;
}

/* /proc/<pid>/statm: "size resident shared text lib data dt" in pages.
 * We don't track residency at PTE granularity, so resident == size.
 * shared/lib/dt are deprecated and reported 0 (matches modern Linux). */
static int prod_statm(char *out, int cap, const struct proc_snap *s) {
    int n = 0;
    n = append_u64(out, cap, n, s->vm_size_pages);
    n = append_str(out, cap, n, " ");
    n = append_u64(out, cap, n, s->vm_size_pages);
    n = append_str(out, cap, n, " 0 ");
    n = append_u64(out, cap, n, s->vm_text_pages);
    n = append_str(out, cap, n, " 0 ");
    n = append_u64(out, cap, n, s->vm_data_pages);
    n = append_str(out, cap, n, " 0\n");
    return n;
}

static int file_uptime_read(struct inode *ip, uint64_t off, void *buf,
                             uint64_t n) {
    (void)ip; return produce_static(prod_uptime, off, buf, n);
}
static int file_meminfo_read(struct inode *ip, uint64_t off, void *buf,
                              uint64_t n) {
    (void)ip; return produce_static(prod_meminfo, off, buf, n);
}
static int file_version_read(struct inode *ip, uint64_t off, void *buf,
                              uint64_t n) {
    (void)ip; return produce_static(prod_version, off, buf, n);
}
static int file_cpuinfo_read(struct inode *ip, uint64_t off, void *buf,
                              uint64_t n) {
    (void)ip; return produce_static(prod_cpuinfo, off, buf, n);
}

static int file_write_rofs(struct inode *ip, uint64_t off, const void *buf,
                            uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n; return -EROFS;
}
static int file_stat_generic(struct inode *ip, struct stat *st) {
    st->st_dev = 3; st->st_ino = (uint64_t)(uintptr_t)ip;
    st->st_mode = ip->mode; st->st_nlink = 1;
    st->st_uid = st->st_gid = 0; st->st_size = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    st->st_blksize = 512;
    st->st_blocks  = 0;
    return 0;
}
static int file_lookup_rofs(struct inode *dir, const char *name,
                             struct inode **out) {
    (void)dir; (void)name; (void)out; return -ENOTDIR;
}
static int file_getdents_rofs(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir; (void)off; (void)buf; (void)n; (void)out_next;
    return -ENOTDIR;
}

static const struct inode_ops piddir_ops = {
    .read     = piddir_read,
    .write    = file_write_rofs,
    .stat     = piddir_stat,
    .lookup   = piddir_lookup,
    .getdents = piddir_getdents,
    .release  = piddir_release,
};

static const struct inode_ops piddir_file_ops = {
    .read     = piddir_file_read,
    .write    = file_write_rofs,
    .stat     = piddir_file_stat,
    .lookup   = file_lookup_rofs,
    .getdents = file_getdents_rofs,
    .release  = piddir_file_release,
};

/* Resolve the symlink target for /proc/<pid>/{cwd,exe,root}. Writes at
 * most cap-1 chars + NUL into `out`. Returns target length (may exceed
 * cap-1 if truncated) or -ESRCH if the pcb is gone. PK_ROOT is constant
 * "/" and never touches the pcb. */
static int pcb_link_target(const struct proc_node *pn, char *out, int cap) {
    if (cap <= 0) return 0;
    out[0] = '\0';
    if (pn->kind == PK_ROOT) {
        if (cap >= 2) { out[0] = '/'; out[1] = '\0'; }
        return 1;
    }
    uint64_t sstatus = procfs_irq_save();
    struct pcb *pcb = proc_find_by_pid(pn->pid);
    if (!pcb || pcb->generation != pn->generation) {
        procfs_irq_restore(sstatus);
        return -ESRCH;
    }
    const char *src = (pn->kind == PK_CWD) ? pcb->cwd_path : pcb->exe_path;
    int slen = 0;
    /* Bound first: && evaluates left-to-right, so reading src[slen]
     * before the bound check would touch src[256] on an unterminated
     * 256-byte path. */
    while (slen < 256 && src[slen]) slen++;
    int n = slen;
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; i++) out[i] = src[i];
    out[n] = '\0';
    procfs_irq_restore(sstatus);
    return slen;
}

static int piddir_link_readlink(struct inode *ip, char *buf, uint64_t n) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (!pn) return -EIO;
    char target[256];
    int slen = pcb_link_target(pn, target, (int)sizeof(target));
    if (slen < 0) return slen;
    int copy = slen;
    if (copy > (int)sizeof(target) - 1) copy = (int)sizeof(target) - 1;
    if ((uint64_t)copy > n) copy = (int)n;
    for (int i = 0; i < copy; i++) buf[i] = target[i];
    return copy;
}

static int piddir_link_stat(struct inode *ip, struct stat *st) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (!pn) return -EIO;
    char target[256];
    int slen = pcb_link_target(pn, target, (int)sizeof(target));
    /* Surface the same -errno (e.g. -ESRCH on stale pcb) that readlink
     * would, so stat()/lstat() can't claim success when readlink fails. */
    if (slen < 0) return slen;
    st->st_dev   = 3;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 0;
    st->st_size  = (uint64_t)slen;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    st->st_blksize = 512;
    st->st_blocks  = (st->st_size + 511) / 512;
    return 0;
}

static const struct inode_ops piddir_link_ops = {
    .stat     = piddir_link_stat,
    .readlink = piddir_link_readlink,
    .release  = piddir_file_release,
};

static struct proc_node *piddir_link_make(int pid, uint64_t gen,
                                           enum proc_kind kind) {
    struct proc_node *pn = pool_alloc();
    if (!pn) return 0;
    pn->kind       = kind;
    pn->pid        = pid;
    pn->generation = gen;

    pn->ino.type        = I_LNK;
    pn->ino.mode        = S_IFLNK | 0777;
    pn->ino.uid         = 0;
    pn->ino.gid         = 0;
    pn->ino.size        = 0;
    pn->ino.mtime       = 0;
    pn->ino.nlink       = 1;
    pn->ino.refcnt      = 1;
    pn->ino.ops         = &piddir_link_ops;
    pn->ino.fs_data     = pn;
    pn->ino.mount_child = 0;
    pn->ino.mount_parent = 0;
    return pn;
}

static int parse_pid(const char *s, int *out_pid) {
    if (!s || !s[0]) return -1;
    int v = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        int digit = s[i] - '0';
        if (v > (2147483647 - digit) / 10) return -1;
        v = v * 10 + digit;
    }
    *out_pid = v;
    return 0;
}

static struct proc_node *piddir_make(int pid, struct pcb *pcb) {
    struct proc_node *pn = pool_alloc();
    if (!pn) return 0;
    pn->kind       = PK_PIDDIR;
    pn->pid        = pid;
    pn->generation = pcb->generation;

    pn->ino.type        = I_DIR;
    pn->ino.mode        = S_IFDIR | 0555;
    pn->ino.uid         = 0;
    pn->ino.gid         = 0;
    pn->ino.size        = 0;
    pn->ino.mtime       = 0;
    pn->ino.nlink       = 1;
    pn->ino.refcnt      = 1;
    pn->ino.ops         = &piddir_ops;
    pn->ino.fs_data     = pn;
    pn->ino.mount_child = 0;
    pn->ino.mount_parent = 0;
    return pn;
}

static struct proc_node *piddir_file_make(int pid, uint64_t gen,
                                           enum proc_kind kind) {
    struct proc_node *pn = pool_alloc();
    if (!pn) return 0;
    pn->kind       = kind;
    pn->pid        = pid;
    pn->generation = gen;

    pn->ino.type        = I_REG;
    pn->ino.mode        = S_IFREG | 0444;
    pn->ino.uid         = 0;
    pn->ino.gid         = 0;
    pn->ino.size        = 0;
    pn->ino.mtime       = 0;
    pn->ino.nlink       = 1;
    pn->ino.refcnt      = 1;
    pn->ino.ops         = &piddir_file_ops;
    pn->ino.fs_data     = pn;
    pn->ino.mount_child = 0;
    pn->ino.mount_parent = 0;
    return pn;
}

static int piddir_stat(struct inode *ip, struct stat *st) {
    return file_stat_generic(ip, st);
}

static void piddir_release(struct inode *ip) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (pn) pool_free(pn);
}

static int piddir_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EISDIR;
}

static int piddir_lookup(struct inode *dir, const char *name,
                         struct inode **out) {
    struct proc_node *pn = (struct proc_node *)dir->fs_data;
    if (name[0] == '.' && name[1] == '\0') {
        *out = inode_get(dir);
        return 0;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(&proc_root_inode);
        return 0;
    }

    enum proc_kind kind = 0;
    int is_link = 0;
    if (strcmp(name, "status") == 0) kind = PK_STATUS;
    else if (strcmp(name, "cmdline") == 0) kind = PK_CMDLINE;
    else if (strcmp(name, "stat") == 0) kind = PK_STAT;
    else if (strcmp(name, "statm") == 0) kind = PK_STATM;
    else if (strcmp(name, "cwd") == 0) { kind = PK_CWD; is_link = 1; }
    else if (strcmp(name, "exe") == 0) { kind = PK_EXE; is_link = 1; }
    else if (strcmp(name, "root") == 0) { kind = PK_ROOT; is_link = 1; }
    else return -ENOENT;

    struct proc_node *file = is_link
        ? piddir_link_make(pn->pid, pn->generation, kind)
        : piddir_file_make(pn->pid, pn->generation, kind);
    if (!file) return -ENOMEM;
    *out = &file->ino;
    return 0;
}

static int emit_dirent(void *buf, uint64_t n, uint64_t off, uint64_t ino,
                        uint8_t dt, const char *name, uint64_t *out_next) {
    int namelen = 0;
    while (name[namelen]) namelen++;
    namelen++;
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) {
        if (out_next) *out_next = off;
        return 0;
    }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino = ino;
    de->d_off = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type = dt;
    for (int j = 0; j < namelen; j++) de->d_name[j] = name[j];

    if (out_next) *out_next = off + 1;
    return reclen;
}

static int piddir_getdents(struct inode *dir, uint64_t off, void *buf,
                            uint64_t n, uint64_t *out_next) {
    static const struct { const char *name; uint8_t dt; } ents[] = {
        { "status",  DT_REG     },
        { "cmdline", DT_REG     },
        { "stat",    DT_REG     },
        { "statm",   DT_REG     },
        { "cwd",     DT_LNK },
        { "exe",     DT_LNK },
        { "root",    DT_LNK },
    };
    if (off >= (uint64_t)(sizeof(ents) / sizeof(ents[0]))) {
        if (out_next) *out_next = off;
        return 0;
    }

    return emit_dirent(buf, n, off, (uint64_t)(uintptr_t)dir, ents[(int)off].dt,
                       ents[(int)off].name, out_next);
}

static int piddir_file_stat(struct inode *ip, struct stat *st) {
    return file_stat_generic(ip, st);
}

static void piddir_file_release(struct inode *ip) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (pn) pool_free(pn);
}

static int piddir_file_read(struct inode *ip, uint64_t off, void *buf,
                             uint64_t n) {
    struct proc_node *pn = (struct proc_node *)ip->fs_data;
    if (!pn) return -ESRCH;

    /* Re-validate the pcb under IRQ-off and snapshot the fields we need.
     * After releasing the critical section the pcb may be reaped, but the
     * snapshot is stable. */
    struct proc_snap snap;
    uint64_t sstatus = procfs_irq_save();
    struct pcb *pcb = proc_find_by_pid(pn->pid);
    if (!pcb || pcb->generation != pn->generation) {
        procfs_irq_restore(sstatus);
        return -ESRCH;
    }
    snap.state      = pcb->state;
    snap.pid        = pcb->pid;
    snap.parent_pid = pcb->parent_pid;
    snap.pgid       = pcb->pgid;
    snap.sid        = pcb->sid;
    for (int i = 0; i < (int)sizeof(snap.comm); i++)
        snap.comm[i] = pcb->comm[i];
    uint64_t vm_bytes = 0;
    uint64_t text_bytes = 0;
    uint64_t data_bytes = 0;
    uint64_t stk_bytes = 0;
    for (struct vma *v = pcb->vma_list; v; v = v->next) {
        /* VMA start is always page-aligned; end may not be (sbrk sets
         * heap_vma->end to the raw brk). Round end up so a partial
         * trailing page is still counted — the kernel will allocate a
         * full page on access. */
        uint64_t bytes = page_round_up(v->end) - v->start;
        vm_bytes += bytes;
        if (v->type == VMA_TYPE_STACK) stk_bytes += bytes;
        if (v->prot & VMA_PROT_X) text_bytes += bytes;
        else                      data_bytes += bytes;
    }
    snap.vm_size_kb    = vm_bytes / 1024;
    snap.vm_stk_kb     = stk_bytes / 1024;
    snap.vm_size_pages = vm_bytes / PAGE_SIZE;
    snap.vm_text_pages = text_bytes / PAGE_SIZE;
    snap.vm_data_pages = data_bytes / PAGE_SIZE;
    procfs_irq_restore(sstatus);

    char tmp[512];
    int total;
    switch (pn->kind) {
    case PK_STATUS:  total = prod_status(tmp, (int)sizeof(tmp), &snap); break;
    case PK_CMDLINE: total = prod_cmdline(tmp, (int)sizeof(tmp), &snap); break;
    case PK_STAT:    total = prod_stat(tmp, (int)sizeof(tmp), &snap); break;
    case PK_STATM:   total = prod_statm(tmp, (int)sizeof(tmp), &snap); break;
    default:         return -ENOENT;
    }

    if (off >= (uint64_t)total) return 0;
    int copy = total - (int)off;
    if ((int)n < copy) copy = (int)n;
    char *dst = (char *)buf;
    for (int i = 0; i < copy; i++) dst[i] = tmp[(int)off + i];
    return copy;
}

#define DEFINE_STATIC_FILE(NAME, READFN)                                       \
    static const struct inode_ops NAME##_ops = {                               \
        .read = READFN, .write = file_write_rofs,                              \
        .stat = file_stat_generic, .lookup = file_lookup_rofs,                 \
        .getdents = file_getdents_rofs,                                        \
    };                                                                         \
    static struct inode NAME##_inode

DEFINE_STATIC_FILE(uptime,  file_uptime_read);
DEFINE_STATIC_FILE(meminfo, file_meminfo_read);
DEFINE_STATIC_FILE(version, file_version_read);
DEFINE_STATIC_FILE(cpuinfo, file_cpuinfo_read);

static int self_readlink(struct inode *ip, char *buf, uint64_t n) {
    (void)ip;
    struct pcb *p = current_proc();
    if (!p) return -EIO;

    char tmp[32];
    int nout = 0;
    static const char prefix[] = "/proc/";
    for (int i = 0; i < (int)sizeof(prefix) - 1 && nout < (int)sizeof(tmp); i++) {
        tmp[nout++] = prefix[i];
    }
    nout += u64_to_dec(tmp + nout, (int)sizeof(tmp) - nout, (uint64_t)p->pid);

    int copy = nout;
    if ((uint64_t)copy > n) copy = (int)n;
    for (int i = 0; i < copy; i++) buf[i] = tmp[i];
    return copy;
}

static uint64_t self_target_len(void) {
    static const char prefix[] = "/proc/";
    struct pcb *p = current_proc();

    if (!p) {
        /* Conservative upper bound for "/proc/" + max uint64 decimal PID. */
        return (uint64_t)((sizeof(prefix) - 1) + 20);
    }

    uint64_t v = (uint64_t)p->pid;
    int digits = 1;
    while (v >= 10) { v /= 10; digits++; }
    return (uint64_t)((sizeof(prefix) - 1) + digits);
}

static int self_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 3;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 0;
    st->st_size = self_target_len();
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    st->st_blksize = 512;
    st->st_blocks  = (st->st_size + 511) / 512;
    return 0;
}

static const struct inode_ops self_ops = {
    .stat     = self_stat,
    .readlink = self_readlink,
};

static struct inode self_inode;

static int proc_root_read(struct inode *ip, uint64_t off, void *buf,
                           uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EISDIR;
}
static int proc_root_write(struct inode *ip, uint64_t off, const void *buf,
                            uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EROFS;
}
static int proc_root_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 3;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 0;
    st->st_size = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    st->st_blksize = 512;
    st->st_blocks  = 0;
    return 0;
}
static int proc_root_lookup(struct inode *dir, const char *name,
                             struct inode **out) {
    (void)dir;
    if (name[0] == '.' && name[1] == '\0') { *out = inode_get(dir); return 0; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(dir); return 0;
    }
    if (name[0] == 's' && name[1] == 'e' && name[2] == 'l' &&
        name[3] == 'f' && name[4] == '\0') {
        *out = inode_get(&self_inode);
        return 0;
    }
    static const struct { const char *name; struct inode *ip; } statics[] = {
        { "uptime",  &uptime_inode  },
        { "meminfo", &meminfo_inode },
        { "version", &version_inode },
        { "cpuinfo", &cpuinfo_inode },
    };
    for (int i = 0; i < 4; i++) {
        const char *s = statics[i].name;
        int j = 0;
        while (s[j] && name[j] && s[j] == name[j]) j++;
        if (s[j] == 0 && name[j] == 0) {
            *out = inode_get(statics[i].ip); return 0;
        }
    }
    int pid;
    if (parse_pid(name, &pid) == 0) {
        struct pcb *pcb = proc_find_by_pid(pid);
        if (!pcb) return -ENOENT;
        struct proc_node *pn = piddir_make(pid, pcb);
        if (!pn) return -ENOMEM;
        *out = &pn->ino;
        return 0;
    }
    return -ENOENT;
}
static int proc_root_getdents(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir;

    static const struct { const char *name; struct inode *ip; uint8_t dt; } stat_ents[] = {
        { "uptime",  &uptime_inode,  DT_REG },
        { "meminfo", &meminfo_inode, DT_REG },
        { "version", &version_inode, DT_REG },
        { "cpuinfo", &cpuinfo_inode, DT_REG },
        { "self",    &self_inode,    DT_UNKNOWN },
    };
    int nstat = 5;

    if (off < (uint64_t)nstat) {
        int i = (int)off;
        int namelen = 0;
        while (stat_ents[i].name[namelen]) namelen++;
        namelen++;
        int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
        if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

        struct dirent64 *de = (struct dirent64 *)buf;
        de->d_ino = (uint64_t)(uintptr_t)stat_ents[i].ip;
        de->d_off = off + 1;
        de->d_reclen = (uint16_t)reclen;
        de->d_type = stat_ents[i].dt;
        for (int j = 0; j < namelen; j++) de->d_name[j] = stat_ents[i].name[j];

        if (out_next) *out_next = off + 1;
        return reclen;
    }

    int idx = (int)(off - (uint64_t)nstat);
    int seen = 0;
    uint64_t irq_state = procfs_irq_save();
    for (struct pcb *p = proc_list_head(); p; p = p->next) {
        if (p->state == PROC_UNUSED) continue;
        if (seen == idx) {
            char name[16];
            int namelen = u64_to_dec(name, (int)sizeof(name) - 1,
                                     (uint64_t)p->pid);
            name[namelen++] = '\0';
            int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
            if ((uint64_t)reclen > n) {
                procfs_irq_restore(irq_state);
                if (out_next) *out_next = off;
                return 0;
            }

            struct dirent64 *de = (struct dirent64 *)buf;
            de->d_ino = (uint64_t)(uintptr_t)p;
            de->d_off = off + 1;
            de->d_reclen = (uint16_t)reclen;
            de->d_type = DT_DIR;
            for (int j = 0; j < namelen; j++) de->d_name[j] = name[j];

            procfs_irq_restore(irq_state);
            if (out_next) *out_next = off + 1;
            return reclen;
        }
        seen++;
    }
    procfs_irq_restore(irq_state);

    if (out_next) *out_next = off;
    return 0;
}

static const struct inode_ops proc_root_ops = {
    .read     = proc_root_read,
    .write    = proc_root_write,
    .stat     = proc_root_stat,
    .lookup   = proc_root_lookup,
    .getdents = proc_root_getdents,
};

void procfs_init(void) {
    self_inode.type        = I_LNK;
    self_inode.mode        = S_IFLNK | 0777;
    self_inode.uid         = 0;
    self_inode.gid         = 0;
    self_inode.size        = 0;
    self_inode.mtime       = 0;
    self_inode.nlink       = 1;
    self_inode.refcnt      = 1;
    self_inode.ops         = &self_ops;
    self_inode.fs_data     = 0;
    self_inode.mount_child = 0;
    self_inode.mount_parent = 0;

    proc_root_inode.type        = I_DIR;
    proc_root_inode.mode        = S_IFDIR | 0555;
    proc_root_inode.uid         = 0;
    proc_root_inode.gid         = 0;
    proc_root_inode.size        = 0;
    proc_root_inode.mtime       = 0;
    proc_root_inode.nlink       = 1;
    proc_root_inode.refcnt      = 1;
    proc_root_inode.ops         = &proc_root_ops;
    proc_root_inode.fs_data     = 0;
    proc_root_inode.mount_child = 0;
    proc_root_inode.mount_parent = 0;

    static const struct {
        struct inode      *ip;
        const struct inode_ops *ops;
    } static_files[] = {
        { &uptime_inode,  &uptime_ops  },
        { &meminfo_inode, &meminfo_ops },
        { &version_inode, &version_ops },
        { &cpuinfo_inode, &cpuinfo_ops },
    };
    for (int i = 0; i < (int)(sizeof(static_files)/sizeof(static_files[0])); i++) {
        struct inode *ip = static_files[i].ip;
        ip->type        = I_REG;
        ip->mode        = S_IFREG | 0444;
        ip->uid = ip->gid = 0;
        ip->size = ip->mtime = 0;
        ip->nlink = 1;
        ip->refcnt = 1;
        ip->ops = static_files[i].ops;
        ip->fs_data = 0;
        ip->mount_child = ip->mount_parent = 0;
    }

}

/* Register the procfs root at `target`. Returns 0 on success, negative
 * errno on failure. Idempotent when called twice with the same target
 * (mount_fs detects same-root re-mount). */
int procfs_attach(const char *target) {
    int rc = mount_fs(target, &proc_root_inode);
    if (rc == 0) printk("procfs: mounted %s\n", target);
    return rc;
}
