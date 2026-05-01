/*
 * rename_test — sys_rename / atomic move-or-rename tests.
 *
 * Coverage:
 *  - same-directory file rename
 *  - cross-directory file move
 *  - replace existing newpath (file→file)
 *  - rename empty directory
 *  - cross-directory directory move + ".." fixup + nlink adjustment
 *  - same-path no-op (rename("/a","/a") == 0)
 *  - error cases:
 *      ENOENT (source missing)
 *      EISDIR / ENOTDIR (cross-type replacement)
 *      ENOTEMPTY (replace non-empty dir)
 *      EINVAL (move dir into its own subtree — loop)
 *      EXDEV (cross-fs)
 *      EROFS (rename onto read-only tarfs)
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int pass = 0, fail = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[rename_test] PASS  %s\n", msg); pass++; }
    else      { printf("[rename_test] FAIL  %s\n", msg); fail++; }
}

static void try_unlink(const char *p) { (void)unlink(p); }

static int file_contents_equal(const char *path, const char *want, long wantlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char buf[64];
    long n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n != wantlen) return 0;
    return memcmp(buf, want, wantlen) == 0;
}

static void write_file(const char *path, const char *data, long n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) { write(fd, data, n); close(fd); }
}

int main(void) {
    /* Pre-clean. */
    try_unlink("/data/rn_a/inner.txt");
    try_unlink("/data/rn_b/inner.txt");
    try_unlink("/data/rn_c/inner.txt");
    try_unlink("/data/rn_a/sub");
    try_unlink("/data/rn_b/sub");
    try_unlink("/data/rn_c/sub");
    try_unlink("/data/rn_a");
    try_unlink("/data/rn_b");
    try_unlink("/data/rn_c");
    try_unlink("/data/rn_d");
    try_unlink("/data/rn_old");
    try_unlink("/data/rn_new");
    try_unlink("/data/rn_replace_me");
    try_unlink("/data/rn_dst");

    /* ---- 1. Same-directory file rename ---- */
    write_file("/data/rn_old", "hello", 5);
    int rc = rename("/data/rn_old", "/data/rn_new");
    chk(rc == 0, "rename /data/rn_old -> /data/rn_new returns 0");
    struct stat st;
    chk(stat("/data/rn_old", &st) < 0, "old name gone after rename");
    chk(stat("/data/rn_new", &st) == 0, "new name exists after rename");
    chk(file_contents_equal("/data/rn_new", "hello", 5),
        "renamed file content preserved");

    /* ---- 2. Same-path no-op ---- */
    rc = rename("/data/rn_new", "/data/rn_new");
    chk(rc == 0, "rename A -> A returns 0 (no-op)");
    chk(stat("/data/rn_new", &st) == 0, "rn_new still exists after no-op rename");

    /* ---- 3. Replace existing file ---- */
    write_file("/data/rn_replace_me", "OLD", 3);
    rc = rename("/data/rn_new", "/data/rn_replace_me");
    chk(rc == 0, "rename onto existing file returns 0 (replace)");
    chk(stat("/data/rn_new", &st) < 0, "rn_new gone after replace");
    chk(file_contents_equal("/data/rn_replace_me", "hello", 5),
        "rn_replace_me has source content (not stale 'OLD')");
    try_unlink("/data/rn_replace_me");

    /* ---- 4. Cross-directory file move ---- */
    chk(mkdir("/data/rn_a", 0755) == 0, "mkdir /data/rn_a for move test");
    chk(mkdir("/data/rn_b", 0755) == 0, "mkdir /data/rn_b for move test");
    write_file("/data/rn_a/inner.txt", "moveme", 6);
    rc = rename("/data/rn_a/inner.txt", "/data/rn_b/inner.txt");
    chk(rc == 0, "cross-dir file move returns 0");
    chk(stat("/data/rn_a/inner.txt", &st) < 0, "old location gone");
    chk(file_contents_equal("/data/rn_b/inner.txt", "moveme", 6),
        "moved file content preserved");
    try_unlink("/data/rn_b/inner.txt");

    /* ---- 5. ENOENT: rename nonexistent ---- */
    rc = rename("/data/rn_does_not_exist", "/data/rn_dst");
    chk(rc < 0, "rename nonexistent source returns negative (ENOENT)");

    /* ---- 6. EISDIR: file replacing dir ---- */
    write_file("/data/rn_old", "x", 1);
    rc = rename("/data/rn_old", "/data/rn_a");      /* rn_a is a dir */
    chk(rc < 0, "file onto dir returns negative (EISDIR)");
    try_unlink("/data/rn_old");

    /* ---- 7. ENOTDIR: dir replacing file ---- */
    write_file("/data/rn_d", "x", 1);
    rc = rename("/data/rn_a", "/data/rn_d");        /* rn_a is dir, rn_d is file */
    chk(rc < 0, "dir onto file returns negative (ENOTDIR)");
    try_unlink("/data/rn_d");

    /* ---- 8. ENOTEMPTY: replace non-empty dir ---- */
    write_file("/data/rn_b/keep.txt", "x", 1);
    rc = rename("/data/rn_a", "/data/rn_b");
    chk(rc < 0, "rename onto non-empty dir returns negative (ENOTEMPTY)");
    try_unlink("/data/rn_b/keep.txt");

    /* ---- 9. Rename empty dir, replacing empty target ---- */
    /* Make rn_b empty again, rename rn_a (empty) onto it. */
    rc = rename("/data/rn_a", "/data/rn_b");
    chk(rc == 0, "rename empty dir onto empty dir succeeds");
    chk(stat("/data/rn_a", &st) < 0, "rn_a gone after dir rename");
    chk(stat("/data/rn_b", &st) == 0 && S_ISDIR(st.st_mode),
        "rn_b still a directory after replace");

    /* ---- 10. Cross-dir directory move + ".." fixup + nlink ---- */
    chk(mkdir("/data/rn_a", 0755) == 0, "remake /data/rn_a");
    chk(mkdir("/data/rn_c", 0755) == 0, "mkdir /data/rn_c");
    chk(mkdir("/data/rn_a/sub", 0755) == 0, "mkdir /data/rn_a/sub");
    /* Before move: rn_a nlink should include sub's "..", rn_c shouldn't. */
    if (stat("/data/rn_a", &st) == 0) {
        chk(st.st_nlink == 3, "rn_a nlink == 3 before move (. + parent + sub/..)");
    }
    if (stat("/data/rn_c", &st) == 0) {
        chk(st.st_nlink == 2, "rn_c nlink == 2 before move (. + parent)");
    }
    rc = rename("/data/rn_a/sub", "/data/rn_c/sub");
    chk(rc == 0, "move directory across parents returns 0");
    /* After move: rn_a loses sub's "..", rn_c gains it. */
    if (stat("/data/rn_a", &st) == 0) {
        chk(st.st_nlink == 2, "rn_a nlink == 2 after sub moved out");
    }
    if (stat("/data/rn_c", &st) == 0) {
        chk(st.st_nlink == 3, "rn_c nlink == 3 after sub moved in");
    }
    /* Sanity: child accessible at new location. */
    chk(stat("/data/rn_c/sub", &st) == 0 && S_ISDIR(st.st_mode),
        "/data/rn_c/sub exists and is a directory");
    chk(stat("/data/rn_a/sub", &st) < 0, "/data/rn_a/sub no longer exists");
    /* Verify ".." in moved dir really points to new parent: cd into it
     * via "..", end up at /data/rn_c. We can sanity-check by creating
     * a file and reaching it through the new path. */
    write_file("/data/rn_c/sub/marker", "M", 1);
    chk(file_contents_equal("/data/rn_c/sub/marker", "M", 1),
        "moved dir is accessible through new parent");

    /* ---- 11. EINVAL: rename dir into own subtree (loop) ---- */
    rc = rename("/data/rn_c", "/data/rn_c/sub/loop");
    chk(rc < 0, "rename dir into own subtree returns negative (EINVAL)");

    /* ---- 12. EXDEV: cross-fs (sbfs ↔ tarfs) ---- */
    rc = rename("/data/rn_b", "/bin/foo");
    chk(rc < 0, "cross-fs rename sbfs->tarfs returns negative (EXDEV)");
    rc = rename("/bin/echo", "/data/rn_dst");
    chk(rc < 0, "cross-fs rename tarfs->sbfs returns negative (EXDEV)");

    /* ---- 13. EROFS: rename within tarfs ---- */
    rc = rename("/bin/echo", "/bin/echo2");
    chk(rc < 0, "rename within read-only tarfs returns negative");

    /* Final cleanup. */
    try_unlink("/data/rn_c/sub/marker");
    try_unlink("/data/rn_c/sub");
    try_unlink("/data/rn_a");
    try_unlink("/data/rn_b");
    try_unlink("/data/rn_c");

    if (fail == 0) printf("rename_test: PASS (%d tests)\n", pass);
    else           printf("rename_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
