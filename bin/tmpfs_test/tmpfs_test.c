/*
 * tmpfs_test — verify the in-memory /tmp filesystem.
 *
 * Coverage:
 *   1. open + write + read round-trip on /tmp
 *   2. mkdir under /tmp + nested files
 *   3. unlink truly removes (stat→ENOENT)
 *   4. ISOLATION: same name in /tmp and /mnt holds different content
 *   5. nlink + link() + rename() (validates VFS dispatch)
 *   6. inode-count limit honored: too many files → -ENOSPC eventually
 *   7. file-size limit honored: writing past 256 KiB → -EFBIG
 *
 * tmpfs is RAM-only; full ephemerality across reboots can only be
 * verified manually. Within one boot, this test exercises the
 * in-memory storage path.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int pass = 0, fail = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[tmpfs_test] PASS  %s\n", msg); pass++; }
    else      { printf("[tmpfs_test] FAIL  %s\n", msg); fail++; }
}

static void try_unlink(const char *p) { (void)unlink(p); }

int main(void) {
    /* Pre-clean. */
    try_unlink("/tmp/foo");
    try_unlink("/tmp/bar");
    try_unlink("/tmp/sub/inner");
    try_unlink("/tmp/sub");

    /* ---- 1. Round-trip ---- */
    int fd = open("/tmp/foo", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /tmp/foo");
    if (fd >= 0) {
        long n = write(fd, "hello tmpfs", 11);
        chk(n == 11, "write 11 bytes to /tmp/foo");
        close(fd);
    }
    fd = open("/tmp/foo", O_RDONLY);
    chk(fd >= 0, "reopen /tmp/foo");
    if (fd >= 0) {
        char buf[16] = {0};
        long n = read(fd, buf, sizeof(buf));
        chk(n == 11 && memcmp(buf, "hello tmpfs", 11) == 0,
            "/tmp/foo content survives round-trip");
        close(fd);
    }

    /* ---- 2. mkdir + nested file ---- */
    int rc = mkdir("/tmp/sub", 0755);
    chk(rc == 0, "mkdir /tmp/sub");

    fd = open("/tmp/sub/inner", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /tmp/sub/inner");
    if (fd >= 0) { write(fd, "x", 1); close(fd); }

    struct stat st;
    chk(stat("/tmp/sub", &st) == 0 && S_ISDIR(st.st_mode),
        "/tmp/sub is a directory");

    /* ---- 2b. ".." from nested dir resolves to the real parent ----
     * stat("/tmp/sub/..") must yield the same inode as stat("/tmp"),
     * not /tmp/sub itself. Catches a bug where the lookup op naively
     * returned `dir` for any "..". */
    struct stat st_root, st_dotdot, st_sub;
    chk(stat("/tmp",        &st_root)   == 0, "stat /tmp");
    chk(stat("/tmp/sub",    &st_sub)    == 0, "stat /tmp/sub");
    chk(stat("/tmp/sub/..", &st_dotdot) == 0, "stat /tmp/sub/..");
    chk(st_dotdot.st_ino == st_root.st_ino,
        "/tmp/sub/.. resolves to /tmp (not /tmp/sub)");
    chk(st_dotdot.st_ino != st_sub.st_ino,
        "/tmp/sub/.. distinct from /tmp/sub");

    /* ---- 3. unlink truly removes ---- */
    chk(unlink("/tmp/foo") == 0, "unlink /tmp/foo");
    chk(stat("/tmp/foo", &st) < 0, "/tmp/foo gone after unlink");

    /* ---- 4. /tmp and /mnt are isolated ---- */
    fd = open("/tmp/iso", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "TMPFS", 5); close(fd); }
    fd = open("/mnt/iso", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "SBFS!", 5); close(fd); }

    char buf[8] = {0};
    fd = open("/tmp/iso", O_RDONLY);
    if (fd >= 0) { read(fd, buf, 5); close(fd); }
    chk(memcmp(buf, "TMPFS", 5) == 0, "/tmp/iso reads its own bytes");

    memset(buf, 0, sizeof(buf));
    fd = open("/mnt/iso", O_RDONLY);
    if (fd >= 0) { read(fd, buf, 5); close(fd); }
    chk(memcmp(buf, "SBFS!", 5) == 0,
        "/mnt/iso unaffected by /tmp/iso (separate filesystems)");

    try_unlink("/tmp/iso");
    try_unlink("/mnt/iso");

    /* ---- 5. nlink, link, rename ---- */
    fd = open("/tmp/nl", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "abc", 3); close(fd); }

    chk(stat("/tmp/nl", &st) == 0 && st.st_nlink == 1,
        "fresh file has nlink == 1");

    rc = link("/tmp/nl", "/tmp/nl2");
    chk(rc == 0, "link /tmp/nl -> /tmp/nl2");
    chk(stat("/tmp/nl",  &st) == 0 && st.st_nlink == 2, "nl  nlink == 2");
    chk(stat("/tmp/nl2", &st) == 0 && st.st_nlink == 2, "nl2 nlink == 2");

    rc = rename("/tmp/nl2", "/tmp/nl3");
    chk(rc == 0, "rename /tmp/nl2 -> /tmp/nl3");
    chk(stat("/tmp/nl2", &st) < 0,        "/tmp/nl2 gone after rename");
    chk(stat("/tmp/nl3", &st) == 0,       "/tmp/nl3 exists after rename");

    try_unlink("/tmp/nl");
    try_unlink("/tmp/nl3");

    /* ---- 6. inode-count limit ---- */
    /* tmpfs caps inodes at TMPFS_NINODES (= 64). One slot is the root,
     * one is /tmp/sub created above, and we may have leftovers from
     * earlier tests. Just create until ENOSPC and verify it's reached. */
    int created = 0;
    int got_enospc = 0;
    for (int i = 0; i < 200; i++) {
        char path[32];
        int n = 0;
        const char *prefix = "/tmp/cap_";
        while (prefix[n]) { path[n] = prefix[n]; n++; }
        /* Manually spell out the integer to avoid sprintf assumptions. */
        int idx = i;
        char digits[8];
        int ndig = 0;
        if (idx == 0) digits[ndig++] = '0';
        while (idx > 0) { digits[ndig++] = '0' + (idx % 10); idx /= 10; }
        for (int j = ndig - 1; j >= 0; j--) path[n++] = digits[j];
        path[n] = 0;

        fd = open(path, O_WRONLY | O_CREAT);
        if (fd < 0) { got_enospc = 1; break; }
        close(fd);
        created++;
    }
    chk(got_enospc, "creation eventually fails with -ENOSPC at inode cap");
    chk(created > 0, "successfully created at least one capacity-test file");
    /* Cleanup capacity-test files. */
    for (int i = 0; i < created; i++) {
        char path[32];
        int n = 0;
        const char *prefix = "/tmp/cap_";
        while (prefix[n]) { path[n] = prefix[n]; n++; }
        int idx = i;
        char digits[8];
        int ndig = 0;
        if (idx == 0) digits[ndig++] = '0';
        while (idx > 0) { digits[ndig++] = '0' + (idx % 10); idx /= 10; }
        for (int j = ndig - 1; j >= 0; j--) path[n++] = digits[j];
        path[n] = 0;
        try_unlink(path);
    }

    /* ---- 7. file-size limit (256 KiB) ---- */
    fd = open("/tmp/big", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /tmp/big");
    if (fd >= 0) {
        char block[1024];
        memset(block, 'A', sizeof(block));
        long total = 0;
        long w;
        /* 256 KiB = 256 blocks of 1 KiB; the 257th block must fail. */
        for (int i = 0; i < 300; i++) {
            w = write(fd, block, sizeof(block));
            if (w <= 0) break;
            total += w;
        }
        close(fd);
        chk(total == 256 * 1024,
            "write fills exactly 256 KiB before hitting EFBIG");
    }
    /* Reopen with O_APPEND so the offset starts at EOF (== MAX), then
     * verify a non-zero write returns a negative error rather than 0.
     * A 0 return would spin POSIX-style writers. */
    fd = open("/tmp/big", O_WRONLY | O_APPEND);
    if (fd >= 0) {
        long extra = write(fd, "x", 1);
        chk(extra < 0,
            "write at MAX_FILESIZE returns -errno, not 0 (would spin)");
        close(fd);
    }
    try_unlink("/tmp/big");

    /* ---- 7b. overlong basename rejected with -ENAMETOOLONG ----
     * Without the length gate, two distinct overlong names sharing
     * the first TMPFS_DIRSIZ bytes alias to the same dirent; here
     * we assert the kernel surfaces the error instead. */
    {
        char longp[80];
        int k = 0;
        const char *pre = "/tmp/";
        while (pre[k]) { longp[k] = pre[k]; k++; }
        for (int j = 0; j < 60; j++) longp[k++] = 'A';
        longp[k] = 0;
        int rc_long = open(longp, O_WRONLY | O_CREAT);
        chk(rc_long < 0, "open(O_CREAT) with 60-char basename rejected");
        int rc_mkd = mkdir(longp, 0755);
        chk(rc_mkd < 0, "mkdir with 60-char basename rejected");
    }

    /* ---- 8. dir nlink accounting: rmdir must reclaim the slot ----
     * Catches the bug where unlink on a directory only dropped one of
     * two nlinks, leaving the inode at nlink == 1 forever and leaking
     * a slot from the fixed pool. Loop more times than TMPFS_NINODES
     * (64) so a leak would exhaust the pool and the next mkdir fails. */
    int dir_iters = 0;
    int dir_ok = 1;
    for (int i = 0; i < 128; i++) {
        char path[24] = "/tmp/dir_x";
        path[9] = 'a' + (i % 26);
        path[10] = 'a' + ((i / 26) % 26);
        path[11] = 0;
        if (mkdir(path, 0755) < 0) { dir_ok = 0; break; }
        if (unlink(path)      < 0) { dir_ok = 0; break; }
        dir_iters++;
    }
    chk(dir_ok && dir_iters == 128,
        "mkdir+rmdir 128 times without exhausting inode pool");

    /* Final cleanup. */
    try_unlink("/tmp/sub/inner");
    try_unlink("/tmp/sub");

    if (fail == 0) printf("tmpfs_test: PASS (%d tests)\n", pass);
    else           printf("tmpfs_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
