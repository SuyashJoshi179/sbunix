#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

/* Pathname expansion. Walks the pattern segment by segment. For each
 * component containing wildcards, opens the directory and matches via
 * fnmatch; recurses on matches. No GLOB_BRACE expansion — BB rarely uses
 * it. No GLOB_TILDE (no $HOME concept on SBUnix yet). */

static int has_magic(const char *s) {
    for (; *s; s++) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
        if (*s == '\\' && s[1]) s++;
    }
    return 0;
}

static int append_path(glob_t *g, const char *path) {
    size_t need = g->gl_offs + g->gl_pathc + 2;
    char **np = realloc(g->gl_pathv, need * sizeof(char *));
    if (!np) return -1;
    g->gl_pathv = np;
    char *dup = strdup(path);
    if (!dup) return -1;
    g->gl_pathv[g->gl_offs + g->gl_pathc] = dup;
    g->gl_pathc++;
    g->gl_pathv[g->gl_offs + g->gl_pathc] = 0;
    return 0;
}

static int strptr_cmp(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* Recursive expansion. `prefix` is the path prefix accumulated so far
 * (already known to exist). `pat` is the remaining pattern. Caller has
 * already stripped a leading '/' if present and reflected it in prefix. */
static int do_glob(const char *prefix, const char *pat, int flags,
                   int (*errfunc)(const char *, int), glob_t *g) {
    /* Find next '/' in pat to isolate this segment. */
    const char *slash = strchr(pat, '/');
    size_t seg_len = slash ? (size_t)(slash - pat) : strlen(pat);
    char seg[256];
    if (seg_len >= sizeof(seg)) { errno = ENAMETOOLONG; return GLOB_NOSPACE; }
    memcpy(seg, pat, seg_len);
    seg[seg_len] = 0;

    char child[PATH_MAX];

    if (!has_magic(seg)) {
        /* Literal segment: append and recurse without opening a dir. */
        if (snprintf(child, sizeof(child), "%s%s%s",
                     prefix,
                     (*prefix && prefix[strlen(prefix)-1] != '/') ? "/" : "",
                     seg) >= (int)sizeof(child)) {
            return GLOB_NOSPACE;
        }
        if (!slash) {
            struct stat st;
            if (stat(child, &st) < 0) return GLOB_NOMATCH;
            return append_path(g, child) < 0 ? GLOB_NOSPACE : 0;
        }
        return do_glob(child, slash + 1, flags, errfunc, g);
    }

    /* Wildcard segment: scan the prefix directory. */
    const char *opendir_path = *prefix ? prefix : ".";
    DIR *d = opendir(opendir_path);
    if (!d) {
        if (errfunc && errfunc(opendir_path, errno)) return GLOB_ABORTED;
        if (flags & GLOB_ERR) return GLOB_ABORTED;
        return GLOB_NOMATCH;
    }
    int fnm_flags = FNM_PATHNAME;
    if (!(flags & GLOB_PERIOD)) fnm_flags |= FNM_PERIOD;
    if (flags & GLOB_NOESCAPE) fnm_flags |= FNM_NOESCAPE;

    int rc = GLOB_NOMATCH;
    int found_any = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        if (fnmatch(seg, name, fnm_flags) != 0) continue;
        found_any = 1;
        if (snprintf(child, sizeof(child), "%s%s%s",
                     prefix,
                     (*prefix && prefix[strlen(prefix)-1] != '/') ? "/" : "",
                     name) >= (int)sizeof(child)) continue;
        if (slash) {
            int r = do_glob(child, slash + 1, flags, errfunc, g);
            if (r == 0) rc = 0;
            else if (r != GLOB_NOMATCH) { closedir(d); return r; }
        } else {
            if (append_path(g, child) < 0) { closedir(d); return GLOB_NOSPACE; }
            rc = 0;
        }
    }
    closedir(d);
    (void)found_any;
    return rc;
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *pglob) {
    if (!pattern || !pglob) return GLOB_NOSYS;
    if (!(flags & GLOB_APPEND)) {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = 0;
        if (!(flags & GLOB_DOOFFS)) pglob->gl_offs = 0;
        if (flags & GLOB_DOOFFS) {
            pglob->gl_pathv = calloc(pglob->gl_offs + 1, sizeof(char *));
            if (!pglob->gl_pathv) return GLOB_NOSPACE;
        }
    }

    size_t start_count = pglob->gl_pathc;
    const char *p = pattern;
    char prefix[PATH_MAX] = "";
    if (*p == '/') {
        prefix[0] = '/'; prefix[1] = 0;
        p++;
        while (*p == '/') p++;
    }
    int rc = do_glob(prefix, p, flags, errfunc, pglob);

    if (rc == GLOB_NOMATCH && (flags & GLOB_NOCHECK)) {
        if (append_path(pglob, pattern) < 0) return GLOB_NOSPACE;
        rc = 0;
    }
    if (rc == 0 && !(flags & GLOB_NOSORT)) {
        size_t n = pglob->gl_pathc - start_count;
        if (n > 1) {
            qsort(pglob->gl_pathv + pglob->gl_offs + start_count,
                  n, sizeof(char *), strptr_cmp);
        }
    }
    return rc;
}

void globfree(glob_t *pglob) {
    if (!pglob || !pglob->gl_pathv) return;
    for (size_t i = 0; i < pglob->gl_pathc; i++) {
        free(pglob->gl_pathv[pglob->gl_offs + i]);
    }
    free(pglob->gl_pathv);
    pglob->gl_pathv = 0;
    pglob->gl_pathc = 0;
}
