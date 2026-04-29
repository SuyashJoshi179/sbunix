#include <sys/statfs.h>
#include <string.h>
#include <errno.h>

/* No SYS_statfs. Return a fixed snapshot describing sbfs at /data: 6 KiB
 * max files, 256 inodes, 512-byte blocks. Good enough for `df` style
 * tools to render *something* without crashing. */

static void fill_statfs(struct statfs *b) {
    memset(b, 0, sizeof(*b));
    b->f_type    = 0x53424653;   /* 'SBFS' */
    b->f_bsize   = 512;
    b->f_blocks  = 4096;
    b->f_bfree   = 4096;
    b->f_bavail  = 4096;
    b->f_files   = 256;
    b->f_ffree   = 256;
    b->f_namelen = 32;
    b->f_frsize  = 512;
}

static void fill_statvfs(struct statvfs *b) {
    memset(b, 0, sizeof(*b));
    b->f_bsize   = 512;
    b->f_frsize  = 512;
    b->f_blocks  = 4096;
    b->f_bfree   = 4096;
    b->f_bavail  = 4096;
    b->f_files   = 256;
    b->f_ffree   = 256;
    b->f_favail  = 256;
    b->f_namemax = 32;
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
