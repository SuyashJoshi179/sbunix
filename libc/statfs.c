#include <sys/statfs.h>
#include <string.h>
#include <errno.h>

/* No SYS_statfs. Return a fixed snapshot matching the real on-disk sbfs
 * geometry (kernel/include/sbfs.h, tools/mkfs.c): 1000 data blocks of
 * 512 bytes, 256 inodes, 14-char dirent names. Good enough for `df`
 * style tools to render *something* consistent without crashing. */
#define SBFS_BSIZE        512
#define SBFS_DATA_BLOCKS  1000
#define SBFS_NINODES      256
#define SBFS_NAMELEN      14

static void fill_statfs(struct statfs *b) {
    memset(b, 0, sizeof(*b));
    b->f_type    = 0x53424653;   /* 'SBFS' */
    b->f_bsize   = SBFS_BSIZE;
    b->f_blocks  = SBFS_DATA_BLOCKS;
    b->f_bfree   = SBFS_DATA_BLOCKS;
    b->f_bavail  = SBFS_DATA_BLOCKS;
    b->f_files   = SBFS_NINODES;
    b->f_ffree   = SBFS_NINODES;
    b->f_namelen = SBFS_NAMELEN;
    b->f_frsize  = SBFS_BSIZE;
}

static void fill_statvfs(struct statvfs *b) {
    memset(b, 0, sizeof(*b));
    b->f_bsize   = SBFS_BSIZE;
    b->f_frsize  = SBFS_BSIZE;
    b->f_blocks  = SBFS_DATA_BLOCKS;
    b->f_bfree   = SBFS_DATA_BLOCKS;
    b->f_bavail  = SBFS_DATA_BLOCKS;
    b->f_files   = SBFS_NINODES;
    b->f_ffree   = SBFS_NINODES;
    b->f_favail  = SBFS_NINODES;
    b->f_namemax = SBFS_NAMELEN;
}

int statfs(const char *path, struct statfs *b) {
    (void)path;
    if (!b) { errno = EFAULT; return -1; }
    fill_statfs(b);
    return 0;
}

int fstatfs(int fd, struct statfs *b) {
    (void)fd;
    if (!b) { errno = EFAULT; return -1; }
    fill_statfs(b);
    return 0;
}

int statvfs(const char *path, struct statvfs *b) {
    (void)path;
    if (!b) { errno = EFAULT; return -1; }
    fill_statvfs(b);
    return 0;
}

int fstatvfs(int fd, struct statvfs *b) {
    (void)fd;
    if (!b) { errno = EFAULT; return -1; }
    fill_statvfs(b);
    return 0;
}
