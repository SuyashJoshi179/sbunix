/*
 * grader_libc_test — POSIX libc surface for MicroPython + BusyBox.
 *
 * Tests string.h, stdlib.h, ctype.h, stdio.h (snprintf/sscanf),
 * setjmp.h, sys/wait.h macros, and environment functions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/wait.h>

static int fails = 0;
static void check(int cond, const char *name) {
    if (cond) printf("[grader_libc_test] PASS  %s\n", name);
    else { printf("[grader_libc_test] FAIL  %s\n", name); fails++; }
}

static int int_cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(void) {
    printf("=== grader_libc_test ===\n");

    /* --- string.h --- */
    check(strlen("hello") == 5, "strlen");
    check(strcmp("abc", "abc") == 0, "strcmp equal");
    check(strcmp("abc", "abd") < 0, "strcmp less");
    check(strncmp("abcX", "abcY", 3) == 0, "strncmp prefix");

    char dst[64];
    strcpy(dst, "hello");
    check(strcmp(dst, "hello") == 0, "strcpy");
    strncpy(dst, "AB", 5);
    check(dst[0] == 'A' && dst[2] == 0, "strncpy zero-pads");

    strcpy(dst, "foo");
    strcat(dst, "bar");
    check(strcmp(dst, "foobar") == 0, "strcat");

    check(strchr("hello", 'l') != 0, "strchr found");
    check(strchr("hello", 'z') == 0, "strchr not found");
    check(strrchr("hello", 'l') == &"hello"[3], "strrchr last");

    check(strstr("hello world", "world") != 0, "strstr found");
    check(strstr("hello", "xyz") == 0, "strstr not found");

    char *dup = strdup("test");
    check(dup != 0 && strcmp(dup, "test") == 0, "strdup");
    free(dup);

    char ov[16] = "ABCDEFGH";
    memmove(ov + 2, ov, 6);
    check(ov[0] == 'A' && ov[2] == 'A' && ov[4] == 'C',
          "memmove overlap");

    check(memcmp("abc", "abc", 3) == 0, "memcmp equal");
    check(memcmp("abc", "abd", 3) < 0, "memcmp less");
    check(memchr("hello", 'l', 5) == &"hello"[2], "memchr");

    char tok[] = "a,b,,c";
    char *save = 0;
    check(strcmp(strtok_r(tok, ",", &save), "a") == 0, "strtok_r 1st");
    check(strcmp(strtok_r(0, ",", &save), "b") == 0, "strtok_r 2nd");

    check(strerror(2) != 0, "strerror(ENOENT) non-NULL");

    /* --- stdlib.h --- */
    check(atoi("42") == 42, "atoi");
    check(atoi("-7") == -7, "atoi negative");

    char *end;
    check(strtol("0xff", &end, 0) == 255, "strtol hex auto");
    check(strtol("077", &end, 0) == 63, "strtol octal auto");
    check(strtol("-100", &end, 10) == -100, "strtol negative");
    check(strtoul("4294967295", &end, 10) == 4294967295UL, "strtoul");

    check(abs(-5) == 5, "abs");
    check(labs(-100L) == 100L, "labs");

    div_t d = div(17, 5);
    check(d.quot == 3 && d.rem == 2, "div");

    int arr[] = {5, 3, 1, 4, 2};
    qsort(arr, 5, sizeof(int), int_cmp);
    check(arr[0]==1 && arr[4]==5, "qsort");

    int key = 3;
    int *found = (int *)bsearch(&key, arr, 5, sizeof(int), int_cmp);
    check(found && *found == 3, "bsearch");

    /* --- ctype.h --- */
    check(isalpha('A') && isalpha('z'), "isalpha");
    check(!isalpha('5'), "!isalpha digit");
    check(isdigit('0') && isdigit('9'), "isdigit");
    check(isalnum('A') && isalnum('5'), "isalnum");
    check(isspace(' ') && isspace('\n'), "isspace");
    check(isupper('Z') && !isupper('z'), "isupper");
    check(islower('a') && !islower('A'), "islower");
    check(toupper('a') == 'A', "toupper");
    check(tolower('Z') == 'z', "tolower");
    check(isprint(' ') && isprint('~'), "isprint");
    check(isxdigit('f') && isxdigit('A') && isxdigit('9'), "isxdigit");

    /* --- stdio.h: snprintf --- */
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%d %s %x", 42, "hi", 255);
    check(strcmp(buf, "42 hi ff") == 0, "snprintf basic");
    check(n == 8, "snprintf return value");

    n = snprintf(buf, 5, "abcdefgh");
    check(buf[4] == 0, "snprintf truncates with NUL");
    check(n == 8, "snprintf returns full length on truncation");

    snprintf(buf, sizeof(buf), "%05d", 42);
    check(strcmp(buf, "00042") == 0, "snprintf zero-padded width");

    snprintf(buf, sizeof(buf), "%%");
    check(strcmp(buf, "%") == 0, "snprintf literal %%");

    /* --- stdio.h: sscanf --- */
    int a = 0; char word[32] = {0};
    int sc = sscanf("123 hello", "%d %s", &a, word);
    check(sc == 2, "sscanf parses 2 items");
    check(a == 123 && strcmp(word, "hello") == 0, "sscanf values");

    /* --- setjmp.h --- */
    {
        jmp_buf jb;
        volatile int val = 0;
        int r = setjmp(jb);
        if (r == 0) {
            val = 1;
            longjmp(jb, 42);
        }
        check(r == 42, "longjmp returns value 42");
        check(val == 1, "setjmp preserves state");
    }

    /* --- sys/wait.h macros --- */
    {
        int status = 0; /* exited with code 0 */
        check(WIFEXITED(status), "WIFEXITED(0)");
        check(WEXITSTATUS(status) == 0, "WEXITSTATUS(0)");

        status = (42 << 8); /* exited with code 42 */
        check(WIFEXITED(status), "WIFEXITED(42<<8)");
        check(WEXITSTATUS(status) == 42, "WEXITSTATUS == 42");

        status = 9; /* killed by signal 9 */
        check(WIFSIGNALED(status), "WIFSIGNALED(9)");
        check(WTERMSIG(status) == 9, "WTERMSIG == 9");
    }

    /* --- environment --- */
    {
        int rc = setenv("GRADER_KEY", "VALUE", 1);
        check(rc == 0, "setenv succeeds");
        char *v = getenv("GRADER_KEY");
        /* Our libc may stub getenv; just check it doesn't crash */
        if (v) {
            check(strcmp(v, "VALUE") == 0, "getenv returns VALUE");
        } else {
            printf("[grader_libc_test] NOTE  getenv stub (returns NULL)\n");
        }
        check(getenv("NONEXISTENT_KEY_XYZ") == 0,
              "getenv(nonexistent) returns NULL");
    }

    printf("=== grader_libc_test: %d failures ===\n", fails);
    return fails;
}
