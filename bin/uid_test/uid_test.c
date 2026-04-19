#include <stdio.h>
#include <unistd.h>

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else       { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    check(getuid()  == 0, "getuid() == 0");
    check(geteuid() == 0, "geteuid() == 0");
    check(getgid()  == 0, "getgid() == 0");
    check(getegid() == 0, "getegid() == 0");
    check(setuid(0)  == 0, "setuid(0) == 0");
    check(setuid(42) == 0, "setuid(42) == 0 (stub)");
    check(getuid()  == 0, "getuid() still 0 after setuid(42)");
    check(setgid(0)  == 0, "setgid(0) == 0");
    check(setgid(99) == 0, "setgid(99) == 0 (stub)");
    printf("uid_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
