#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

/* Path manipulation and tempfile helpers. realpath does lexical
 * resolution only — no symlink follow until tarfs gains link support. */

/* realpath: produce an absolute, normalized form of `path`. If `out` is
 * NULL, allocate PATH_MAX bytes; caller frees. Returns out (or new buf)
 * on success, NULL on error. Does not follow symlinks. */
char *realpath(const char *path, char *out) {
    if (!path || !*path) { errno = ENOENT; return 0; }

    char *buf = out ? out : (char *)malloc(PATH_MAX);
    if (!buf) { errno = ENOMEM; return 0; }

    char abs[PATH_MAX];
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(abs)) goto toolong;
        strcpy(abs, path);
    } else {
        if (getcwd(abs, sizeof(abs)) == 0) { if (!out) free(buf); return 0; }
        size_t cl = strlen(abs);
        if (cl + 1 + strlen(path) >= sizeof(abs)) goto toolong;
        if (cl == 0 || abs[cl - 1] != '/') { abs[cl++] = '/'; abs[cl] = 0; }
        strcpy(abs + cl, path);
    }

    /* Normalize: walk components, collapse . / .. / empty. */
    char *w = buf;
    *w++ = '/';
    const char *r = abs + 1;
    while (*r) {
        const char *seg = r;
        while (*r && *r != '/') r++;
        size_t sl = r - seg;
        if (sl == 0 || (sl == 1 && seg[0] == '.')) {
            /* skip */
        } else if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            if (w > buf + 1) {
                w--;
                while (w > buf + 1 && w[-1] != '/') w--;
            }
        } else {
            if ((size_t)(w - buf) + sl + 1 >= PATH_MAX) goto toolong;
            memcpy(w, seg, sl);
            w += sl;
            *w++ = '/';
        }
        if (*r == '/') r++;
    }
    if (w > buf + 1 && w[-1] == '/') w--;
    *w = 0;

    /* Verify path exists — POSIX requires ENOENT if no entry. */
    struct stat st;
    if (stat(buf, &st) < 0) { if (!out) free(buf); return 0; }
    return buf;

toolong:
    if (!out) free(buf);
    errno = ENAMETOOLONG;
    return 0;
}

/* Substitute trailing "XXXXXX" (6 chars) with quasi-random alnum chars
 * derived from clock + pid + retry counter. Caller decides what to do
 * with the resulting name (open vs mkdir). */
static const char tmp_alnum[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static int fill_template(char *tmpl, unsigned tries) {
    size_t n = strlen(tmpl);
    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t seed = (uint64_t)ts.tv_nsec ^ ((uint64_t)ts.tv_sec << 20)
                  ^ ((uint64_t)getpid() << 8) ^ tries;
    char *p = tmpl + n - 6;
    for (int i = 0; i < 6; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        p[i] = tmp_alnum[(seed >> 33) % 62];
    }
    return 0;
}

int mkstemp(char *tmpl) {
    for (unsigned t = 0; t < 1000; t++) {
        if (fill_template(tmpl, t) < 0) return -1;
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

char *mkdtemp(char *tmpl) {
    for (unsigned t = 0; t < 1000; t++) {
        if (fill_template(tmpl, t) < 0) return 0;
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (errno != EEXIST) return 0;
    }
    errno = EEXIST;
    return 0;
}
