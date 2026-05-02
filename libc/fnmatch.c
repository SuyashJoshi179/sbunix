#include <fnmatch.h>
#include <string.h>
#include <ctype.h>

/* fnmatch — POSIX shell wildcard matcher. Recursive backtracking; fine for
 * typical pattern lengths. Supports *, ?, [abc], [a-z], [!abc], escape with
 * backslash, plus FNM_PATHNAME, FNM_NOESCAPE, FNM_PERIOD, FNM_CASEFOLD,
 * FNM_LEADING_DIR. */

static int chr_eq(int a, int b, int flags) {
    if (flags & FNM_CASEFOLD) {
        a = tolower((unsigned char)a);
        b = tolower((unsigned char)b);
    }
    return a == b;
}

/* Match a single bracket expression starting at *p (just past '['). On
 * success, advances *p past the closing ']' and returns 1 if c matches.
 * Returns -1 on malformed bracket. */
static int match_bracket(const char **pp, int c, int flags) {
    const char *p = *pp;
    int negate = 0;
    if (*p == '!' || *p == '^') { negate = 1; p++; }
    int matched = 0;
    int first = 1;
    while (*p && (first || *p != ']')) {
        first = 0;
        int lo = (unsigned char)*p++;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && *p) lo = (unsigned char)*p++;
        int hi = lo;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++;
            hi = (unsigned char)*p++;
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && *p) hi = (unsigned char)*p++;
        }
        int cc = c;
        if (flags & FNM_CASEFOLD) {
            cc = tolower(cc);
            lo = tolower(lo);
            hi = tolower(hi);
        }
        if (cc >= lo && cc <= hi) matched = 1;
    }
    if (*p != ']') return -1;
    p++;
    *pp = p;
    return matched ^ negate;
}

static int do_fnmatch(const char *p, const char *s, int flags) {
    /* FNM_PERIOD: a leading '.' in s must match literally — wildcards reject. */
    int leading = (flags & FNM_PERIOD) && (*s == '.');

    while (*p) {
        char pc = *p;
        if (pc == '*') {
            while (*p == '*') p++;
            if (!*p) {
                /* Trailing star: match rest, but FNM_PATHNAME forbids '/'. */
                if (flags & FNM_PATHNAME) {
                    for (; *s; s++) if (*s == '/') return FNM_NOMATCH;
                }
                if (flags & FNM_LEADING_DIR) return 0;
                return 0;
            }
            /* leading '.' rule: '*' may not match leading '.' under FNM_PERIOD */
            if (leading) return FNM_NOMATCH;
            /* try match at each position */
            for (; *s; s++) {
                if ((flags & FNM_PATHNAME) && *s == '/' && *p != '/') {
                    /* '*' cannot cross '/' under FNM_PATHNAME */
                    return do_fnmatch(p, s, flags & ~FNM_PERIOD);
                }
                if (do_fnmatch(p, s, flags & ~FNM_PERIOD) == 0) return 0;
            }
            return do_fnmatch(p, s, flags & ~FNM_PERIOD);
        }
        if (!*s) return FNM_NOMATCH;
        if (pc == '?') {
            if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
            if (leading) return FNM_NOMATCH;
            p++; s++; leading = 0; continue;
        }
        if (pc == '[') {
            if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
            if (leading) return FNM_NOMATCH;
            const char *pp = p + 1;
            int r = match_bracket(&pp, (unsigned char)*s, flags);
            if (r <= 0) {
                if (r < 0) {
                    /* malformed: literal '[' */
                    if (!chr_eq(pc, *s, flags)) return FNM_NOMATCH;
                    p++; s++; leading = 0; continue;
                }
                return FNM_NOMATCH;
            }
            p = pp; s++; leading = 0; continue;
        }
        if (pc == '\\' && !(flags & FNM_NOESCAPE) && p[1]) {
            p++;
            pc = *p;
        }
        if (!chr_eq(pc, *s, flags)) {
            if ((flags & FNM_LEADING_DIR) && *s == '/' && !*p) return 0;
            return FNM_NOMATCH;
        }
        p++; s++;
        leading = 0;
        if ((flags & FNM_PERIOD) && s[-1] == '/') leading = (*s == '.');
    }
    if (!*s) return 0;
    if ((flags & FNM_LEADING_DIR) && *s == '/') return 0;
    return FNM_NOMATCH;
}

int fnmatch(const char *pattern, const char *string, int flags) {
    if (!pattern || !string) return FNM_NOMATCH;
    return do_fnmatch(pattern, string, flags);
}
