#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <vfs.h>
#include <vmem.h>
#include <string.h>
#include <printk.h>
#include <procfs.h>

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
    /* No entries yet; filled in later tasks. */
    return -ENOENT;
}
static int proc_root_getdents(struct inode *dir, uint64_t off, void *buf,
                               uint64_t n, uint64_t *out_next) {
    (void)dir; (void)buf; (void)n;
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

    mount_fs("/proc", &proc_root_inode);
    printk("procfs: mounted /proc\n");
}
