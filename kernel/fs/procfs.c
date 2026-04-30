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
    return 0;
}
static int proc_root_lookup(struct inode *dir, const char *name,
                             struct inode **out) {
    (void)dir;
    if (name[0] == '.' && name[1] == '\0') { *out = inode_get(dir); return 0; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(dir); return 0;
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
    return -ENOENT;
}
static int proc_root_getdents(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir;

    static const struct { const char *name; struct inode *ip; uint8_t dt; } ents[] = {
        { "uptime",  &uptime_inode,  DT_REG },
        { "meminfo", &meminfo_inode, DT_REG },
        { "version", &version_inode, DT_REG },
        { "cpuinfo", &cpuinfo_inode, DT_REG },
    };
    int nent = 4;
    if (off >= (uint64_t)nent) { if (out_next) *out_next = off; return 0; }

    int i = (int)off;
    int namelen = 0;
    while (ents[i].name[namelen]) namelen++;
    namelen++;
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino = (uint64_t)(uintptr_t)ents[i].ip;
    de->d_off = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type = ents[i].dt;
    for (int j = 0; j < namelen; j++) de->d_name[j] = ents[i].name[j];

    if (out_next) *out_next = off + 1;
    return reclen;
}

static const struct inode_ops proc_root_ops = {
    .read     = proc_root_read,
    .write    = proc_root_write,
    .stat     = proc_root_stat,
    .lookup   = proc_root_lookup,
    .getdents = proc_root_getdents,
};

static struct inode proc_root_inode;

void procfs_init(void) {
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

    mount_fs("/proc", &proc_root_inode);
    printk("procfs: mounted /proc\n");
}
