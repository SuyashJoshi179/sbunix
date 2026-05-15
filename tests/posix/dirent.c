/*
 * POSIX conformance test: <dirent.h>
 *
 * Reference: docs/susv5-html/basedefs/dirent.h.html
 *
 * Audited POSIX functions: opendir, fdopendir, readdir, readdir_r, closedir,
 *                          rewinddir, dirfd, seekdir, telldir, scandir, alphasort
 *
 * Excluded (POSIX function not implemented): posix_getdents
 * Excluded (non-POSIX): getdents64, struct dirent64, DT_* (Linux extensions)
 *
 * POSIX requires only `d_name` member of struct dirent. Our libc adds
 * d_ino/d_off/d_reclen/d_type (Linux convention); pin only d_name.
 */
#include <dirent.h>

#define PIN __attribute__((unused)) static

PIN DIR           *(*_pin_opendir)(const char *) = opendir;
PIN DIR           *(*_pin_fdopendir)(int) = fdopendir;
PIN struct dirent *(*_pin_readdir)(DIR *) = readdir;
PIN int            (*_pin_readdir_r)(DIR *, struct dirent *, struct dirent **) = readdir_r;
PIN int            (*_pin_closedir)(DIR *) = closedir;
PIN void           (*_pin_rewinddir)(DIR *) = rewinddir;
PIN int            (*_pin_dirfd)(DIR *) = dirfd;
PIN long           (*_pin_telldir)(DIR *) = telldir;
PIN void           (*_pin_seekdir)(DIR *, long) = seekdir;
PIN int            (*_pin_scandir)(const char *, struct dirent ***,
                                   int (*)(const struct dirent *),
                                   int (*)(const struct dirent **, const struct dirent **)) = scandir;
PIN int            (*_pin_alphasort)(const struct dirent **, const struct dirent **) = alphasort;

__attribute__((unused))
static void _struct_fields(void) {
    struct dirent d;
    __builtin_memset(&d, 0, sizeof d);
    (void)d.d_name[0];
}
