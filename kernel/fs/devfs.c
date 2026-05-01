#include <inode.h>
#include <stat.h>
#include <errno.h>
#include <riscv.h>
#include <termios.h>
#include <vfs.h>
#include <vmem.h>
#include <string.h>
#include <drivers/uart.h>
#include <printk.h>

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* ----------------------------------------------------------------
 * Console inode ops
 * ---------------------------------------------------------------- */

static int console_read(struct inode *ip, uint64_t off, void *buf, uint64_t n) {
    (void)ip; (void)off;
    char *p = (char *)buf;
    struct termios tio;
    termios_get(&tio);
    int canonical = (tio.c_lflag & ICANON) != 0;

    for (uint64_t i = 0; i < n; ) {
        char c;
        int r = uart_rx_get(&c);
        if (r == -EINTR) return i > 0 ? (int)i : -EINTR;
        if (r < 0) return (int)i;    // error
        if (r == 0) {                // EOF (Ctrl-D)
            if (i == 0) return 0;
            return (int)i;
        }
        p[i++] = c;
        if (canonical && c == '\n') return (int)i;
    }
    return (int)n;
}

static int console_write(struct inode *ip, uint64_t off, const void *buf,
                          uint64_t n) {
    (void)ip; (void)off;
    const char *p = (const char *)buf;
    for (uint64_t i = 0; i < n; i++) write_char(p[i]);
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

static int console_ioctl(struct inode *ip, int cmd, unsigned long arg) {
    (void)ip;
    if (!arg) return -EFAULT;

    uint64_t sstatus = read_sstatus();
    write_sstatus(sstatus & ~SSTATUS_SIE);

    int rc = 0;
    switch (cmd) {
        case TCGETS: {
            struct termios t;
            termios_get(&t);
            if (copyout((void *)arg, &t, sizeof(t)) < 0) rc = -EFAULT;
            break;
        }
        case TCSETS: {
            struct termios t;
            if (copyin(&t, (void *)arg, sizeof(t)) < 0) {
                rc = -EFAULT;
                break;
            }
            termios_set(&t);
            break;
        }
        case TIOCGWINSZ: {
            struct winsize ws = {24, 80, 0, 0};
            if (copyout((void *)arg, &ws, sizeof(ws)) < 0) rc = -EFAULT;
            break;
        }
        case TIOCSPGRP: {
            int pid;
            if (copyin(&pid, (void *)arg, sizeof(pid)) < 0) {
                rc = -EFAULT;
                break;
            }
            termios_set_fg_pid(pid);
            break;
        }
        case TIOCGPGRP: {
            int pid = termios_get_fg_pid();
            if (copyout((void *)arg, &pid, sizeof(pid)) < 0) rc = -EFAULT;
            break;
        }
        default:
            rc = -EINVAL;
            break;
    }

    write_sstatus(sstatus);
    return rc;
}

static const struct inode_ops console_ops = {
    .read     = console_read,
    .write    = console_write,
    .ioctl    = console_ioctl,
    .stat     = console_stat,
    .lookup   = console_lookup,
    .getdents = console_getdents,
};

/* ----------------------------------------------------------------
 * devfs root directory ops (one entry: "console")
 * ---------------------------------------------------------------- */

// The console inode (static, never freed).
static struct inode console_inode;

/* ----------------------------------------------------------------
 * /dev/loop — self-referential symlink, used by the symlink test
 * to exercise ELOOP detection. readlink returns "/dev/loop", so
 * resolution loops until namei hits SYMLINK_MAX.
 * ---------------------------------------------------------------- */
static int loop_readlink(struct inode *ip, char *buf, uint64_t n) {
    (void)ip;
    static const char target[] = "/dev/loop";
    int len = (int)sizeof(target) - 1;  /* 9, no NUL */
    int copy = (int)n < len ? (int)n : len;
    for (int i = 0; i < copy; i++) buf[i] = target[i];
    return copy;
}

static int loop_stat(struct inode *ip, struct stat *st) {
    st->st_dev   = 2;
    st->st_ino   = (uint64_t)(uintptr_t)ip;
    st->st_mode  = ip->mode;
    st->st_nlink = 1;
    st->st_uid   = st->st_gid = 0;
    st->st_size  = 9;       /* strlen("/dev/loop") */
    st->st_atime = st->st_mtime = st->st_ctime = 0;
    return 0;
}

static const struct inode_ops loop_ops = {
    .stat     = loop_stat,
    .readlink = loop_readlink,
};

static struct inode loop_inode;

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
    if (streq(name, "loop")) {
        *out = inode_get(&loop_inode);
        return 0;
    }
    return -ENOENT;
}
static int devroot_getdents(struct inode *dir, uint64_t off, void *buf,
                             uint64_t n, uint64_t *out_next) {
    (void)dir;

    static const struct {
        const char *name;
        struct inode *ino;
        uint8_t d_type;
    } ents[] = {
        { "console", &console_inode, DT_CHR },
        { "loop",    &loop_inode,    DT_UNKNOWN },  /* DT_LNK not defined */
    };
    int nent = (int)(sizeof(ents) / sizeof(ents[0]));

    if (off >= (uint64_t)nent) { if (out_next) *out_next = off; return 0; }

    int i = (int)off;
    int namelen = 0;
    while (ents[i].name[namelen]) namelen++;
    namelen++;                                       /* include NUL */
    int reclen = (DIRENT64_FIXED_LEN + namelen + 7) & ~7;
    if ((uint64_t)reclen > n) { if (out_next) *out_next = off; return 0; }

    struct dirent64 *de = (struct dirent64 *)buf;
    memset(de, 0, reclen);
    de->d_ino    = (uint64_t)(uintptr_t)ents[i].ino;
    de->d_off    = off + 1;
    de->d_reclen = (uint16_t)reclen;
    de->d_type   = ents[i].d_type;
    for (int j = 0; j < namelen; j++) de->d_name[j] = ents[i].name[j];

    if (out_next) *out_next = off + 1;
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
    devroot_inode.mount_parent = 0;

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
    console_inode.mount_parent = 0;

    loop_inode.type        = I_LNK;
    loop_inode.mode        = S_IFLNK | 0777;
    loop_inode.uid         = 0;
    loop_inode.gid         = 0;
    loop_inode.size        = 9;
    loop_inode.mtime       = 0;
    loop_inode.nlink       = 1;
    loop_inode.refcnt      = 1;
    loop_inode.ops         = &loop_ops;
    loop_inode.fs_data     = 0;
    loop_inode.mount_child = 0;
    loop_inode.mount_parent = 0;

    // Mount devfs at /dev (requires tarfs "/" to already be mounted).
    mount_fs("/dev", &devroot_inode);
    printk("devfs: mounted /dev\n");
}
