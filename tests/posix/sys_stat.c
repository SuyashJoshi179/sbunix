/*
 * POSIX conformance test: <sys/stat.h>
 *
 * Reference: docs/susv5-html/basedefs/sys_stat.h.html
 *
 * Audited POSIX functions: stat, fstat, lstat, chmod, fchmod, umask,
 *                          mkfifo, mknod
 * Excluded (not implemented): fstatat, fchmodat, mkfifoat, mkdirat, mknodat,
 *                             futimens, utimensat, mkdir (in unistd in our libc)
 *
 * struct stat: POSIX requires st_atim/st_mtim/st_ctim (struct timespec);
 * our libc uses st_atime/st_mtime/st_ctime (uint64). DIVERGENCE — see
 * commented pin below.
 */
#include <sys/stat.h>

#define PIN __attribute__((unused)) static

PIN int (*_pin_stat)(const char *, struct stat *) = stat;
PIN int (*_pin_fstat)(int, struct stat *) = fstat;
PIN int (*_pin_lstat)(const char *, struct stat *) = lstat;
PIN int (*_pin_chmod)(const char *, mode_t) = chmod;
PIN int (*_pin_fchmod)(int, mode_t) = fchmod;
PIN mode_t (*_pin_umask)(mode_t) = umask;
PIN int (*_pin_mkfifo)(const char *, mode_t) = mkfifo;
PIN int (*_pin_mknod)(const char *, mode_t, dev_t) = mknod;

__attribute__((unused))
static void _struct_fields(void) {
    struct stat st;
    __builtin_memset(&st, 0, sizeof st);
    (void)st.st_dev; (void)st.st_ino; (void)st.st_mode; (void)st.st_nlink;
    (void)st.st_uid; (void)st.st_gid; (void)st.st_size; (void)st.st_rdev;
    (void)st.st_blksize; (void)st.st_blocks;

    (void)st.st_atim.tv_sec; (void)st.st_atim.tv_nsec;
    (void)st.st_mtim.tv_sec; (void)st.st_mtim.tv_nsec;
    (void)st.st_ctim.tv_sec; (void)st.st_ctim.tv_nsec;
}

__attribute__((unused))
static void _macro_checks(mode_t m) {
    int x = 0;
    x |= S_IFMT; x |= S_IFREG; x |= S_IFDIR; x |= S_IFCHR;
    x |= S_IFBLK; x |= S_IFIFO; x |= S_IFLNK; x |= S_IFSOCK;
    x |= S_ISUID; x |= S_ISGID; x |= S_ISVTX;
    x |= S_IRWXU; x |= S_IRUSR; x |= S_IWUSR; x |= S_IXUSR;
    x |= S_IRWXG; x |= S_IRGRP; x |= S_IWGRP; x |= S_IXGRP;
    x |= S_IRWXO; x |= S_IROTH; x |= S_IWOTH; x |= S_IXOTH;
    x |= S_ISREG(m); x |= S_ISDIR(m); x |= S_ISCHR(m);
    x |= S_ISBLK(m); x |= S_ISFIFO(m); x |= S_ISLNK(m); x |= S_ISSOCK(m);
    (void)x;
}
