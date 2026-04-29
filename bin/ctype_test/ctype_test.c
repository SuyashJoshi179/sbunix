#include <stdio.h>
#include <ctype.h>

static int truth_isdigit(int c)  { return c >= '0' && c <= '9'; }
static int truth_isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static int truth_islower(int c)  { return c >= 'a' && c <= 'z'; }
static int truth_isalpha(int c)  { return truth_isupper(c) || truth_islower(c); }
static int truth_isalnum(int c)  { return truth_isalpha(c) || truth_isdigit(c); }
static int truth_isxdigit(int c) {
    return truth_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int truth_isspace(int c)  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
static int truth_isblank(int c)  { return c == ' ' || c == '\t'; }
static int truth_iscntrl(int c)  { return ((unsigned)c < 32) || c == 127; }
static int truth_isprint(int c)  { return c >= 32 && c < 127; }
static int truth_isgraph(int c)  { return c > 32  && c < 127; }
static int truth_ispunct(int c)  { return truth_isprint(c) && !truth_isalnum(c) && c != ' '; }

#define CHECK(fn)                                                             \
    do {                                                                      \
        for (int c = 0; c < 128; c++) {                                       \
            int got  = !!fn(c);                                               \
            int want = !!truth_##fn(c);                                       \
            if (got != want) {                                                \
                printf("ctype_test: %s(%d) got=%d want=%d\n", #fn, c, got, want); \
                return 1;                                                     \
            }                                                                 \
        }                                                                     \
    } while (0)

int main(void) {
    CHECK(isdigit);
    CHECK(isupper);
    CHECK(islower);
    CHECK(isalpha);
    CHECK(isalnum);
    CHECK(isxdigit);
    CHECK(isspace);
    CHECK(isblank);
    CHECK(iscntrl);
    CHECK(isprint);
    CHECK(isgraph);
    CHECK(ispunct);

    for (int c = 'A'; c <= 'Z'; c++)
        if (tolower(c) != c + ('a' - 'A')) { printf("ctype_test: tolower(%c) wrong\n", c); return 1; }
    for (int c = 'a'; c <= 'z'; c++)
        if (toupper(c) != c - ('a' - 'A')) { printf("ctype_test: toupper(%c) wrong\n", c); return 1; }
    if (tolower('5') != '5') { printf("ctype_test: tolower digit\n"); return 1; }
    if (toupper('5') != '5') { printf("ctype_test: toupper digit\n"); return 1; }

    printf("ctype_test: PASS\n");
    return 0;
}
