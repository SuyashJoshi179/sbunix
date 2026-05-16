#include <stdio.h>
#include <unistd.h>

static int pass, fail;

static void check(int cond, const char *name) {
    if (cond) { printf("PASS  %s\n", name); pass++; }
    else      { printf("FAIL  %s\n", name); fail++; }
}

int main(void) {
    check(getuid()  == 0, "getuid() starts at 0");
    check(geteuid() == 0, "geteuid() starts at 0");
    check(getgid()  == 0, "getgid() starts at 0");
    check(getegid() == 0, "getegid() starts at 0");

    check(setuid(0)  == 0, "setuid(0) == 0");
    check(setuid(42) == 0, "setuid(42) == 0");
    check(getuid()  == 42, "getuid() reflects setuid(42)");
    check(geteuid() == 42, "geteuid() reflects setuid(42)");

    check(setgid(0)  == 0, "setgid(0) == 0");
    check(setgid(99) == 0, "setgid(99) == 0");
    check(getgid()  == 99, "getgid() reflects setgid(99)");
    check(getegid() == 99, "getegid() reflects setgid(99)");

    printf("uid_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
