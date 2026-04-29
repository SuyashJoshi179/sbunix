#include <mntent.h>
#include <string.h>
#include <stdio.h>

/* Hardcoded mount table: tarfs at / and sbfs at /data. We dont have
 * /etc/mtab on disk, so setmntent ignores its arguments and walks this
 * fixed list once. */

static const struct mntent table[] = {
    { (char *)"tarfs", (char *)"/",     (char *)"tarfs", (char *)"ro,defaults", 0, 0 },
    { (char *)"sbfs",  (char *)"/data", (char *)"sbfs",  (char *)"rw,defaults", 0, 0 },
};

#define TABLE_LEN (int)(sizeof(table) / sizeof(table[0]))

/* We use the FILE* opaquely as a cursor — caller treats it as a token. */
static int cursor;

FILE *setmntent(const char *file, const char *mode) {
    (void)file; (void)mode;
    cursor = 0;
    return (FILE *)&cursor;
}

int endmntent(FILE *fp) {
    (void)fp;
    cursor = 0;
    return 1;
}

struct mntent *getmntent(FILE *fp) {
    (void)fp;
    if (cursor >= TABLE_LEN) return 0;
    return (struct mntent *)&table[cursor++];
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
