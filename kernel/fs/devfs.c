#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <drivers/uart.h>
#include <console.h>
#include <printk.h>

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* ----------------------------------------------------------------
 * Console inode ops
 * ---------------------------------------------------------------- */

static int console_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; (void)off;
    return cons_read((char *)buf, n);
}

static int console_write(struct inode *ip, uint64_t off, const void *buf,
                          uint64_t n) {
    (void)ip; (void)off;
    const char *p = (const char *)buf;
    for (uint64_t i = 0; i < n; i++) console_putc(p[i]);
    return (int)n;
}

static int console_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 2;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_size  = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}

static int console_lookup(struct inode *dir, const char *name,
                           struct inode **out) {
    (void)dir; (void)name; (void)out;
    return -ENOTDIR;
}

static int console_getdents(struct inode *dir, uint64_t off, void *buf,
                              uint64_t n, uint64_t *out_next) {
    (void)dir; (void)off; (void)buf; (void)n; (void)out_next;
    return -ENOTDIR;
}

static const struct inode_ops console_ops = {
    .read     = console_read,
    .write    = console_write,
    .stat     = console_stat,
    .lookup   = console_lookup,
    .getdents = console_getdents,
};

/* ----------------------------------------------------------------
 * devfs root directory ops (one entry: "console")
 * ---------------------------------------------------------------- */

// The console inode (static, never freed).
static struct inode console_inode;

static int devroot_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EISDIR;
}
static int devroot_write(struct inode *ip, uint64_t off, const void *buf,
                          uint64_t n) {
    (void)ip; (void)off; (void)buf; (void)n;
    return -EISDIR;
}
static int devroot_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 2;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid   = st->st_gid = 0;
    st->st_size  = 0;
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}
static int devroot_lookup(struct inode *dir, const char *name,
                           struct inode **out) {
    (void)dir;
    if (name[0] == '.' && name[1] == '\0') { *out = inode_get(dir); return 0; }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        *out = inode_get(dir); return 0;  // ".." of /dev stays at /dev
    }
    if (streq(name, "console")) {
        *out = inode_get(&console_inode);
        return 0;
    }
    return -ENOENT;
}
static int devroot_getdents(struct inode *dir, uint64_t off, void *buf,
                             uint64_t n, uint64_t *out_next) {
    (void)dir;
    if (off > 0) { if (out_next) *out_next = off; return 0; }

    int         namelen = 8;  // strlen("console") + 1 (null terminator)
    int         reclen  = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;

    if ((uint64_t)reclen > n) { if (out_next) *out_next = 0; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    de->d_ino    = (uint64_t)(uintptr_t)&console_inode;
    de->d_off    = 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type   = DT_CHR;
    de->d_name[0] = 'c'; de->d_name[1] = 'o'; de->d_name[2] = 'n';
    de->d_name[3] = 's'; de->d_name[4] = 'o'; de->d_name[5] = 'l';
    de->d_name[6] = 'e'; de->d_name[7] = '\0';

    if (out_next) *out_next = 1;
    return reclen;
}

static const struct inode_ops devroot_ops = {
    .read     = devroot_read,
    .write    = devroot_write,
    .stat     = devroot_stat,
    .lookup   = devroot_lookup,
    .getdents = devroot_getdents,
};

static struct inode devroot_inode;

/* ----------------------------------------------------------------
 * devfs_init — called from kernel.c after tarfs_init
 * ---------------------------------------------------------------- */

// Exported so that proc_spawn / sys_open can get the console inode directly.
struct inode *devfs_console_inode(void) { return &console_inode; }

void devfs_init(void) {
    // Initialise the /dev root directory inode.
    devroot_inode.type        = I_DIR;
    devroot_inode.mode        = S_IFDIR | 0755;
    devroot_inode.uid         = 0;
    devroot_inode.gid         = 0;
    devroot_inode.size        = 0;
    devroot_inode.mtime       = 0;
    devroot_inode.nlink       = 1;
    devroot_inode.refcnt      = 1;
    devroot_inode.ops         = &devroot_ops;
    devroot_inode.fs_data     = 0;
    devroot_inode.mount_child = 0;

    // Initialise the /dev/console inode.
    console_inode.type        = I_CHR;
    console_inode.mode        = S_IFCHR | 0666;
    console_inode.uid         = 0;
    console_inode.gid         = 0;
    console_inode.size        = 0;
    console_inode.mtime       = 0;
    console_inode.nlink       = 1;
    console_inode.refcnt      = 1;
    console_inode.ops         = &console_ops;
    console_inode.fs_data     = 0;
    console_inode.mount_child = 0;

    // Mount devfs at /dev (requires tarfs "/" to already be mounted).
    mount_fs("/dev", &devroot_inode);
    printk("devfs: mounted /dev/console\n");
}
