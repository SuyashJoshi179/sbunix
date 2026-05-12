#include <mntent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Hardcoded mount table: every fs the kernel attaches at boot. We don't
 * have /etc/mtab on disk, so setmntent ignores its arguments and walks
 * this fixed list once per opened stream. Keep in sync with selftest.c's
 * mount_fs calls in main_setup. */

static const struct mntent table[] = {
    { (char *)"tarfs", (char *)"/",     (char *)"tarfs", (char *)"ro,defaults", 0, 0 },
    { (char *)"sbfs",  (char *)"/mnt",  (char *)"sbfs",  (char *)"rw,defaults", 0, 0 },
    { (char *)"tmpfs", (char *)"/tmp",  (char *)"tmpfs", (char *)"rw,defaults", 0, 0 },
};

#define TABLE_LEN (int)(sizeof(table) / sizeof(table[0]))

/* Mirror the first two ints of `struct _FILE` (see libc/printf.c) so the
 * pointer we hand back is also a valid FILE*: fileno() reads `fd` (-1,
 * a benign sentinel) and fclose() inspects `flags` (no _FILE_OWNED, so
 * it no-ops). The cursor lives after that header — one cursor per
 * stream, so concurrent setmntent calls don't trample each other. */
struct mntent_handle {
    int fd;
    int flags;
    int cursor;
};

FILE *setmntent(const char *file, const char *mode) {
    (void)file; (void)mode;
    struct mntent_handle *h = malloc(sizeof(*h));
    if (!h) return 0;
    h->fd = -1;
    h->flags = 0;
    h->cursor = 0;
    return (FILE *)h;
}

int endmntent(FILE *fp) {
    free(fp);
    return 1;
}

struct mntent *getmntent(FILE *fp) {
    struct mntent_handle *h = (struct mntent_handle *)fp;
    if (!h || h->cursor >= TABLE_LEN) return 0;
    return (struct mntent *)&table[h->cursor++];
}

struct mntent *getmntent_r(FILE *fp, struct mntent *m, char *buf, int buflen) {
    (void)buf; (void)buflen;
    struct mntent *src = getmntent(fp);
    if (!src || !m) return 0;
    *m = *src;
    return m;
}

int addmntent(FILE *fp, const struct mntent *m) {
    (void)fp; (void)m;
    return 1;  /* always fail per glibc convention */
}

char *hasmntopt(const struct mntent *m, const char *opt) {
    if (!m || !m->mnt_opts || !opt) return 0;
    size_t ol = strlen(opt);
    char *p = m->mnt_opts;
    while (*p) {
        char *next = strchr(p, ',');
        size_t span = next ? (size_t)(next - p) : strlen(p);
        if (span == ol && strncmp(p, opt, ol) == 0) return p;
        if (!next) break;
        p = next + 1;
    }
    return 0;
}
