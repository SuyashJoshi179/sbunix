// Shared glob pattern matcher used by the shell and its tests.
// Header-only so test binaries can pull it in without linking sh.c.
//
// Supported syntax:
//   *        matches any sequence (including empty)
//   ?        matches any single character
//   [abc]    matches one character from the set
//   [a-z]    matches one character in the range
//   [!abc]   matches one character NOT in the set
//   \\c      matches the literal character c (escapes *, ?, [)
//
// '/' is treated as an ordinary character — callers split paths.

#ifndef SH_GLOB_MATCH_H
#define SH_GLOB_MATCH_H

static int glob_class_match(const char *pat, char c, const char **endp) {
    // pat points just past '['. Returns 1/0; *endp set to char after closing ']'.
    int negate = 0;
    if (*pat == '!' || *pat == '^') { negate = 1; pat++; }
    int found = 0;
    // A literal ']' as the first char is allowed and matches a ']'.
    int first = 1;
    while (*pat && (first || *pat != ']')) {
        char lo = *pat++;
        char hi = lo;
        if (*pat == '-' && pat[1] && pat[1] != ']') {
            pat++;
            hi = *pat++;
        }
        if (c >= lo && c <= hi) found = 1;
        first = 0;
    }
    if (*pat == ']') pat++;
    *endp = pat;
    return negate ? !found : found;
}

static int glob_match(const char *pat, const char *name) {
    if (*pat == 0) return *name == 0;
    if (*pat == '*') {
        while (*pat == '*') pat++;
        if (*pat == 0) return 1;
        // Try to match the rest at every position in name.
        do {
            if (glob_match(pat, name)) return 1;
        } while (*name++);
        return 0;
    }
    if (*pat == '?') {
        if (*name == 0) return 0;
        return glob_match(pat + 1, name + 1);
    }
    if (*pat == '[') {
        if (*name == 0) return 0;
        const char *next;
        if (!glob_class_match(pat + 1, *name, &next)) return 0;
        return glob_match(next, name + 1);
    }
    if (*pat == '\\' && pat[1]) {
        if (pat[1] != *name) return 0;
        return glob_match(pat + 2, name + 1);
    }
    if (*name == 0) return 0;
    if (*pat != *name) return 0;
    return glob_match(pat + 1, name + 1);
}

static int glob_has_meta(const char *s) {
    while (*s) {
        if (*s == '*' || *s == '?' || *s == '[') return 1;
        if (*s == '\\' && s[1]) s++;
        s++;
    }
    return 0;
}

#endif
