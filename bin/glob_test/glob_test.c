/*
 * glob_test — unit tests for the shell's glob_match() pattern matcher.
 *
 * Covers '*' (any sequence), '?' (single char), character classes
 * '[abc]', ranges '[a-z]', negation '[!abc]', backslash escapes, and
 * combinations. Imports the header directly so it doesn't need to fork
 * the shell — gives precise failure messages per pattern.
 */
#include <stdio.h>
#include <string.h>

#include "../sh/glob_match.h"

static int pass_cnt = 0, fail_cnt = 0;

static void chk_match(const char *pat, const char *name, int expect) {
    int got = glob_match(pat, name);
    if (!!got == !!expect) {
        printf("[glob_test] PASS  '%s' %s '%s'\n",
               pat, expect ? "matches" : "rejects", name);
        pass_cnt++;
    } else {
        printf("[glob_test] FAIL  '%s' should %s '%s' (got %d)\n",
               pat, expect ? "match" : "reject", name, got);
        fail_cnt++;
    }
}

static void chk_meta(const char *s, int expect) {
    int got = glob_has_meta(s);
    if (!!got == !!expect) {
        printf("[glob_test] PASS  has_meta('%s') == %d\n", s, expect);
        pass_cnt++;
    } else {
        printf("[glob_test] FAIL  has_meta('%s') expected %d, got %d\n",
               s, expect, got);
        fail_cnt++;
    }
}

int main(void) {
    /* ---- Literal matching ---- */
    chk_match("foo.c",   "foo.c",   1);
    chk_match("foo.c",   "foo.h",   0);
    chk_match("",        "",        1);
    chk_match("",        "x",       0);
    chk_match("abc",     "ab",      0);
    chk_match("abc",     "abcd",    0);

    /* ---- '*' wildcard ---- */
    chk_match("*",       "",        1);
    chk_match("*",       "anything",1);
    chk_match("*.c",     "main.c",  1);
    chk_match("*.c",     "main.h",  0);
    chk_match("*.c",     ".c",      1);
    chk_match("foo*",    "foobar",  1);
    chk_match("foo*",    "foo",     1);
    chk_match("foo*",    "fo",      0);
    chk_match("*foo*",   "xfooy",   1);
    chk_match("*foo*",   "fo",      0);
    chk_match("**",      "anyx",    1);   /* ** treated as * */
    chk_match("a*b*c",   "axxbyyc", 1);
    chk_match("a*b*c",   "abc",     1);
    chk_match("a*b*c",   "axbxd",   0);

    /* ---- '?' wildcard ---- */
    chk_match("?",       "",        0);
    chk_match("?",       "a",       1);
    chk_match("?",       "ab",      0);
    chk_match("foo?.c",  "foo1.c",  1);
    chk_match("foo?.c",  "foo.c",   0);     /* '?' requires one char */
    chk_match("foo?.c",  "foo12.c", 0);
    chk_match("a?c",     "abc",     1);
    chk_match("a?c",     "ac",      0);
    chk_match("?*",      "",        0);     /* needs at least 1 char */
    chk_match("?*",      "x",       1);

    /* ---- Character classes ---- */
    chk_match("[abc]",     "a",       1);
    chk_match("[abc]",     "b",       1);
    chk_match("[abc]",     "c",       1);
    chk_match("[abc]",     "d",       0);
    chk_match("[abc]",     "",        0);
    chk_match("[abc]*.c",  "afoo.c",  1);
    chk_match("[abc]*.c",  "dfoo.c",  0);

    /* Ranges */
    chk_match("[a-z]",     "m",       1);
    chk_match("[a-z]",     "M",       0);
    chk_match("[A-Z]",     "Q",       1);
    chk_match("[0-9]",     "5",       1);
    chk_match("[0-9]",     "x",       0);
    chk_match("file[0-9].c", "file3.c", 1);
    chk_match("file[0-9].c", "filex.c", 0);

    /* Negation */
    chk_match("[!abc]",    "d",       1);
    chk_match("[!abc]",    "a",       0);
    chk_match("[^0-9]",    "x",       1);
    chk_match("[^0-9]",    "5",       0);

    /* Mixed sets */
    chk_match("[a-zA-Z_]*", "Hello",  1);
    chk_match("[a-zA-Z_]*", "_init",  1);
    chk_match("[a-zA-Z_]*", "9foo",   0);

    /* ---- Escaping ---- */
    chk_match("\\*",       "*",       1);
    chk_match("\\*",       "x",       0);
    chk_match("\\?",       "?",       1);
    chk_match("\\?",       "a",       0);
    chk_match("foo\\*",    "foo*",    1);
    chk_match("foo\\*",    "foobar",  0);

    /* ---- Combos / regression cases ---- */
    chk_match("*.[ch]",    "main.c",  1);
    chk_match("*.[ch]",    "main.h",  1);
    chk_match("*.[ch]",    "main.o",  0);
    chk_match("test_*.c",  "test_a.c",1);
    chk_match("test_*.c",  "test.c",  0);
    chk_match("a*z",       "az",      1);
    chk_match("a*z",       "aXYZz",   1);
    chk_match("a*z",       "ab",      0);

    /* ---- has_meta detector ---- */
    chk_meta("plain.txt", 0);
    chk_meta("*.c",       1);
    chk_meta("foo?.c",    1);
    chk_meta("[abc]",     1);
    chk_meta("\\*literal",0);   /* escaped star — no live meta */
    chk_meta("",          0);

    printf("[glob_test] %d passed, %d failed\n", pass_cnt, fail_cnt);
    return fail_cnt ? 1 : 0;
}
