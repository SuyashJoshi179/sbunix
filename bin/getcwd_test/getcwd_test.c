// getcwd_test: verify getcwd() reflects the current working directory.
//
// Tests:
//   1. Initial cwd is "/".
//   2. After chdir("/bin"), getcwd returns a string starting with "/bin".
//   3. After chdir("/"), getcwd returns "/" again.
//   4. getcwd with an undersized buffer returns a negative error.
#include <stdio.h>
#include <unistd.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[getcwd_test] PASS %s\n", msg); pass_cnt++; }
    else       { printf("[getcwd_test] FAIL %s\n", msg); fail_cnt++; }
}

// Minimal strcmp for comparing path strings.
static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return (a[i] == '\0' && b[i] == '\0');
}

int main(void) {
    char buf[256];

    // 1. Freshly spawned process — cwd should be "/".
    long n = getcwd(buf, sizeof(buf));
    chk(n >= 0, "getcwd returns >= 0");
    chk(streq(buf, "/"), "initial cwd is '/'");

    // 2. After chdir("/bin"), getcwd should begin with "/bin".
    int rc = chdir("/bin");
    chk(rc == 0, "chdir /bin returns 0");

    n = getcwd(buf, sizeof(buf));
    chk(n >= 0, "getcwd after chdir /bin returns >= 0");
    // cwd_path is stored verbatim from the chdir argument, so it should be "/bin".
    chk(buf[0]=='/' && buf[1]=='b' && buf[2]=='i' && buf[3]=='n' &&
        (buf[4]=='\0' || buf[4]=='/'),
        "getcwd returns /bin after chdir /bin");

    // 3. After chdir("/"), cwd should be "/" again.
    rc = chdir("/");
    chk(rc == 0, "chdir / returns 0");

    n = getcwd(buf, sizeof(buf));
    chk(streq(buf, "/"), "getcwd returns / after chdir /");

    // 4. Buffer too small: "/" needs 2 bytes (char + NUL); 1 byte is too small.
    n = getcwd(buf, 1);
    chk(n < 0, "getcwd with 1-byte buffer returns error");

    if (fail_cnt == 0)
        printf("getcwd_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("getcwd_test: FAIL (%d/%d failed)\n", fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
