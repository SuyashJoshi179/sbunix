#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* DIR holds a per-stream getdents64 buffer. We refill on demand and hand
 * out one POSIX `struct dirent` per readdir call. The buffer is sized so
 * a typical sbfs/tarfs directory fits in a single getdents64 syscall. */
#define DIR_BUF_SZ 1024

struct __DIR {
    int  fd;
    long buf_pos;
    long buf_len;
    long stream_off;
    char buf[DIR_BUF_SZ];
    struct dirent ent;
};

static DIR *dir_alloc(int fd) {
    DIR *d = malloc(sizeof(DIR));
    if (!d) { close(fd); return 0; }
    d->fd = fd;
    d->buf_pos = 0;
    d->buf_len = 0;
    d->stream_off = 0;
    return d;
}

DIR *opendir(const char *path) {
    if (!path) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    return dir_alloc(fd);
}

DIR *fdopendir(int fd) {
    if (fd < 0) return 0;
    return dir_alloc(fd);
}

int closedir(DIR *d) {
    if (!d) return -1;
    int r = close(d->fd);
    free(d);
    return r;
}

int dirfd(DIR *d) { return d ? d->fd : -1; }

static int dir_refill(DIR *d) {
    long n = getdents64(d->fd, d->buf, DIR_BUF_SZ);
    if (n <= 0) { d->buf_pos = d->buf_len = 0; return (int)n; }
    d->buf_pos = 0;
    d->buf_len = n;
    return 1;
}

struct dirent *readdir(DIR *d) {
    if (!d) return 0;
    if (d->buf_pos >= d->buf_len) {
        int r = dir_refill(d);
        if (r <= 0) return 0;
    }
    struct dirent64 *de = (struct dirent64 *)(d->buf + d->buf_pos);
    if (de->d_reclen == 0 || d->buf_pos + de->d_reclen > d->buf_len) return 0;
    d->ent.d_ino = de->d_ino;
    d->ent.d_off = (int64_t)de->d_off;
    d->ent.d_reclen = de->d_reclen;
    d->ent.d_type = de->d_type;
    /* d_name in dirent64 is flexible array; copy up to 255 chars + NUL. */
    size_t name_max = de->d_reclen - (size_t)((char *)de->d_name - (char *)de);
    if (name_max > sizeof(d->ent.d_name) - 1) name_max = sizeof(d->ent.d_name) - 1;
    size_t i;
    for (i = 0; i < name_max && de->d_name[i]; i++) d->ent.d_name[i] = de->d_name[i];
    d->ent.d_name[i] = '\0';
    d->buf_pos += de->d_reclen;
    d->stream_off++;
    return &d->ent;
}

int readdir_r(DIR *d, struct dirent *ent, struct dirent **result) {
    if (!d || !ent || !result) return EINVAL;
    struct dirent *r = readdir(d);
    if (!r) { *result = 0; return 0; }
    *ent = *r;
    *result = ent;
    return 0;
}

void rewinddir(DIR *d) {
    if (!d) return;
    lseek(d->fd, 0, SEEK_SET);
    d->buf_pos = d->buf_len = 0;
    d->stream_off = 0;
}

long telldir(DIR *d) { return d ? d->stream_off : -1; }

void seekdir(DIR *d, long off) {
    if (!d) return;
    rewinddir(d);
    while (d->stream_off < off && readdir(d)) { }
}
