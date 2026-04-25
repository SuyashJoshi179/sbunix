/*
 * mkdir_test — focused tests for sys_mkdir / sbfs directory creation.
 *
 * Targets bug-prone areas:
 *   - basic create + stat reports S_IFDIR
 *   - new dir contains "." and ".." (and only those)
 *   - nlink accounting on both child and parent
 *   - parent directory listing shows the new entry
 *   - EEXIST on re-create (over both dir and file)
 *   - ENOENT under nonexistent parent
 *   - ENOTDIR when parent component is a regular file
 *   - EROFS on tarfs mount
 *   - nested creation (parent must exist first)
 *   - cleanup: unlink empty dir succeeds, then re-mkdir succeeds
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[mkdir_test] PASS  %s\n", msg); pass_cnt++; }
    else      { printf("[mkdir_test] FAIL  %s\n", msg); fail_cnt++; }
}

/* Best-effort cleanup of a path. Ignore errors. */
static void try_unlink(const char *p) { (void)unlink(p); }

/* Walk a directory's entries via getdents64 and report whether each of the
 * names in `wanted` was seen. `wanted` is NULL-terminated. `seen[]` must be
 * the same length and is set to 1 for each matched name. Returns total
 * entries visited (including unmatched). */
static int scan_dir(const char *path, const char **wanted, int *seen,
                    int *n_unexpected) {
    int total = 0;
    if (n_unexpected) *n_unexpected = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[512];
    long n;
    while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            struct dirent64 *de = (struct dirent64 *)(buf + off);
            int matched = 0;
            for (int i = 0; wanted[i]; i++) {
                if (strcmp(de->d_name, wanted[i]) == 0) {
                    seen[i] = 1;
                    matched = 1;
                    break;
                }
            }
            if (!matched && n_unexpected) (*n_unexpected)++;
            total++;
            off += de->d_reclen;
        }
    }
    close(fd);
    return total;
}

int main(void) {
    /* Pre-clean from any prior run (sbfs persists across reboots). */
    try_unlink("/data/mkd_a/inside.txt");
    try_unlink("/data/mkd_a/sub");
    try_unlink("/data/mkd_a");
    try_unlink("/data/mkd_b");
    try_unlink("/data/mkd_c");
    try_unlink("/data/mkd_file");
    try_unlink("/data/mkd_ts");
    try_unlink("/data/mkd_ts2");

    /* ---- 1. Basic mkdir succeeds ---- */
    int rc = mkdir("/data/mkd_a", 0755);
    chk(rc == 0, "mkdir /data/mkd_a returns 0");

    /* ---- 2. fstat reports S_IFDIR with nlink == 2 ---- */
    int fd = open("/data/mkd_a", O_RDONLY);
    chk(fd >= 0, "open /data/mkd_a");
    if (fd >= 0) {
        struct stat st;
        int sr = fstat(fd, &st);
        chk(sr == 0, "fstat /data/mkd_a returns 0");
        chk(S_ISDIR(st.st_mode), "stat: mode is S_IFDIR");
        chk(st.st_nlink == 2, "stat: nlink == 2 (\".\"  + parent's \"..\")");
        close(fd);
    }

    /* ---- 3. New dir contains exactly "." and ".." ---- */
    {
        const char *want[] = { ".", "..", 0 };
        int seen[] = { 0, 0 };
        int extras = 0;
        int total = scan_dir("/data/mkd_a", want, seen, &extras);
        chk(total >= 2, "getdents on new dir returns >= 2 entries");
        chk(seen[0] == 1, "new dir contains \".\"");
        chk(seen[1] == 1, "new dir contains \"..\"");
        chk(extras == 0, "new dir contains no other entries");
    }

    /* ---- 4. Parent's listing now contains "mkd_a" ---- */
    {
        const char *want[] = { "mkd_a", 0 };
        int seen[] = { 0 };
        int total = scan_dir("/data", want, seen, 0);
        chk(total > 0, "getdents on /data returns entries");
        chk(seen[0] == 1, "/data listing contains \"mkd_a\"");
    }

    /* ---- 5. Re-create same path: EEXIST (rc < 0) ---- */
    rc = mkdir("/data/mkd_a", 0755);
    chk(rc < 0, "mkdir over existing dir returns negative");

    /* ---- 6. mkdir over an existing regular file fails ---- */
    fd = open("/data/mkd_file", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "x", 1); close(fd); }
    rc = mkdir("/data/mkd_file", 0755);
    chk(rc < 0, "mkdir over existing regular file returns negative");

    /* ---- 7. mkdir under a regular file: parent is not a dir → fails ---- */
    rc = mkdir("/data/mkd_file/child", 0755);
    chk(rc < 0, "mkdir under regular file returns negative (ENOTDIR)");

    /* ---- 8. mkdir under nonexistent parent: ENOENT ---- */
    rc = mkdir("/data/no_such_dir/child", 0755);
    chk(rc < 0, "mkdir under nonexistent parent returns negative (ENOENT)");

    /* ---- 9. mkdir on tarfs (read-only): EROFS ---- */
    rc = mkdir("/bin/cant_create", 0755);
    chk(rc < 0, "mkdir on read-only tarfs returns negative (EROFS)");

    /* ---- 10. Nested mkdir: parent must already exist ---- */
    rc = mkdir("/data/mkd_a/sub", 0755);
    chk(rc == 0, "mkdir nested /data/mkd_a/sub returns 0");

    /* ---- 11. After nesting, parent's nlink is bumped to 3 ---- */
    fd = open("/data/mkd_a", O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0) {
            chk(st.st_nlink == 3,
                "parent nlink == 3 after one subdir (\".\" + parent + sub's \"..\")");
        }
        close(fd);
    }

    /* ---- 12. File inside the new dir works ---- */
    fd = open("/data/mkd_a/inside.txt", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create file inside new dir");
    if (fd >= 0) {
        long n = write(fd, "hello", 5);
        chk(n == 5, "write inside new dir");
        close(fd);
    }
    fd = open("/data/mkd_a/inside.txt", O_RDONLY);
    if (fd >= 0) {
        char b[8] = {0};
        long n = read(fd, b, 5);
        chk(n == 5 && b[0] == 'h' && b[4] == 'o', "read back from inside new dir");
        close(fd);
    }

    /* ---- 13. unlink the non-empty dir must fail ---- */
    rc = unlink("/data/mkd_a");
    chk(rc < 0, "unlink non-empty dir returns negative (ENOTEMPTY)");

    /* ---- 14. Cleanup, then re-mkdir the same path succeeds ---- */
    try_unlink("/data/mkd_a/inside.txt");
    try_unlink("/data/mkd_a/sub");
    rc = unlink("/data/mkd_a");
    chk(rc == 0, "unlink empty dir succeeds");
    rc = mkdir("/data/mkd_a", 0755);
    chk(rc == 0, "re-mkdir same path after unlink succeeds");

    /* ---- 15. Trailing-slash handling (POSIX: "/foo/" == "/foo") ---- */
    rc = mkdir("/data/mkd_ts/", 0755);
    chk(rc == 0, "mkdir with trailing slash succeeds");

    /* The resulting dir must be reachable at the no-slash path. */
    fd = open("/data/mkd_ts", O_RDONLY);
    chk(fd >= 0, "trailing-slash dir reachable at no-slash path");
    if (fd >= 0) close(fd);

    /* unlink with trailing slash should also work. */
    rc = unlink("/data/mkd_ts/");
    chk(rc == 0, "unlink with trailing slash succeeds");

    /* Multiple trailing slashes should also be stripped. */
    rc = mkdir("/data/mkd_ts2///", 0755);
    chk(rc == 0, "mkdir with multiple trailing slashes succeeds");
    try_unlink("/data/mkd_ts2");

    /* "/" alone has no leaf — must fail cleanly, not panic. */
    rc = mkdir("/", 0755);
    chk(rc < 0, "mkdir / returns negative (no leaf)");

    /* ---- 16. Final cleanup so re-runs across reboots stay clean ---- */
    try_unlink("/data/mkd_a");
    try_unlink("/data/mkd_file");
    try_unlink("/data/mkd_ts");
    try_unlink("/data/mkd_ts2");

    if (fail_cnt == 0)
        printf("mkdir_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("mkdir_test: FAIL (%d/%d failed)\n",
               fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
