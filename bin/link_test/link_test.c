/*
 * link_test — sys_link / hard-link end-to-end tests.
 *
 * Verifies:
 *  - basic link creates a second name pointing to the same inode
 *  - nlink increments on both names; unlink decrements correctly
 *  - data written through one name is visible through the other
 *  - unlinking one name leaves the inode reachable via the other
 *  - EEXIST when newpath already exists
 *  - EPERM when target is a directory
 *  - cross-fs / read-only fs combinations are rejected
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int pass = 0, fail = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[link_test] PASS  %s\n", msg); pass++; }
    else      { printf("[link_test] FAIL  %s\n", msg); fail++; }
}

static void try_unlink(const char *p) { (void)unlink(p); }

int main(void) {
    /* Pre-clean so reruns across reboots stay clean. */
    try_unlink("/data/lt_a");
    try_unlink("/data/lt_b");
    try_unlink("/data/lt_c");
    try_unlink("/data/lt_dir2");
    try_unlink("/data/lt_dir");

    /* ---- 1. Create lt_a with content "hello". ---- */
    int fd = open("/data/lt_a", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /data/lt_a");
    if (fd >= 0) { write(fd, "hello", 5); close(fd); }

    struct stat st;
    chk(stat("/data/lt_a", &st) == 0 && st.st_nlink == 1,
        "lt_a nlink == 1 before link");

    /* ---- 2. link lt_a → lt_b succeeds. ---- */
    int rc = link("/data/lt_a", "/data/lt_b");
    chk(rc == 0, "link /data/lt_a -> /data/lt_b returns 0");

    /* ---- 3. nlink == 2 on both names. ---- */
    chk(stat("/data/lt_a", &st) == 0 && st.st_nlink == 2,
        "lt_a nlink == 2 after link");
    chk(stat("/data/lt_b", &st) == 0 && st.st_nlink == 2,
        "lt_b nlink == 2 after link");

    /* ---- 4. Same inode (st_ino matches). ---- */
    struct stat st2;
    if (stat("/data/lt_a", &st) == 0 && stat("/data/lt_b", &st2) == 0)
        chk(st.st_ino == st2.st_ino, "lt_a and lt_b share inode");

    /* ---- 5. Read from lt_b returns "hello". ---- */
    fd = open("/data/lt_b", O_RDONLY);
    chk(fd >= 0, "open /data/lt_b");
    if (fd >= 0) {
        char buf[16] = {0};
        long n = read(fd, buf, sizeof(buf));
        chk(n == 5 && memcmp(buf, "hello", 5) == 0, "lt_b reads 'hello'");
        close(fd);
    }

    /* ---- 6. Modify through lt_a, see change through lt_b. ---- */
    fd = open("/data/lt_a", O_WRONLY | O_TRUNC);
    if (fd >= 0) { write(fd, "world", 5); close(fd); }
    fd = open("/data/lt_b", O_RDONLY);
    if (fd >= 0) {
        char buf[8] = {0};
        long n = read(fd, buf, 5);
        chk(n == 5 && memcmp(buf, "world", 5) == 0,
            "lt_b sees update written through lt_a (shared inode)");
        close(fd);
    }

    /* ---- 7. unlink lt_a; lt_b still readable, nlink drops to 1. ---- */
    chk(unlink("/data/lt_a") == 0, "unlink lt_a");
    chk(stat("/data/lt_a", &st) < 0, "lt_a gone after unlink");
    chk(stat("/data/lt_b", &st) == 0 && st.st_nlink == 1,
        "lt_b nlink == 1 after lt_a unlinked");
    fd = open("/data/lt_b", O_RDONLY);
    chk(fd >= 0, "lt_b still openable after lt_a removed");
    if (fd >= 0) close(fd);

    /* ---- 8. Final unlink frees the inode. ---- */
    chk(unlink("/data/lt_b") == 0, "unlink lt_b");
    chk(stat("/data/lt_b", &st) < 0, "lt_b gone");

    /* ---- 9. EEXIST: link onto an existing path. ---- */
    fd = open("/data/lt_a", O_WRONLY | O_CREAT);
    if (fd >= 0) close(fd);
    fd = open("/data/lt_b", O_WRONLY | O_CREAT);
    if (fd >= 0) close(fd);
    rc = link("/data/lt_a", "/data/lt_b");
    chk(rc < 0, "link onto existing path returns negative (EEXIST)");

    /* ---- 10. EPERM: hard link to a directory. ---- */
    chk(mkdir("/data/lt_dir", 0755) == 0, "mkdir lt_dir for EPERM test");
    rc = link("/data/lt_dir", "/data/lt_dir2");
    chk(rc < 0, "hard link to directory returns negative (EPERM)");

    /* ---- 11. EXDEV: cross-fs link target on tarfs, parent on sbfs. ---- */
    rc = link("/bin/echo", "/data/lt_c");
    chk(rc < 0, "cross-fs link tarfs->sbfs returns negative (EXDEV)");

    /* ---- 12. EROFS: target on sbfs, newpath parent on tarfs. ---- */
    rc = link("/data/lt_a", "/bin/lt_x");
    chk(rc < 0, "link onto read-only tarfs returns negative");

    /* Final cleanup. */
    try_unlink("/data/lt_a");
    try_unlink("/data/lt_b");
    try_unlink("/data/lt_dir");

    if (fail == 0) printf("link_test: PASS (%d tests)\n", pass);
    else           printf("link_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
