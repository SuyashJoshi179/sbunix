/*
 * timestamp_test — verify sbfs records and updates inode mtime correctly.
 *
 * sbfs v1 stores a single on-disk timestamp; sbfs_op_stat reports it as
 * all three POSIX fields (atime/mtime/ctime). This test checks:
 *
 *   1. New file has a non-zero, recent mtime (RTC is plumbed in)
 *   2. atime == mtime == ctime  (documented single-timestamp scheme)
 *   3. mtime advances after write
 *   4. mtime advances after truncate
 *   5. mtime advances on the *target* of link (nlink change)
 *   6. parent dir's mtime advances after mkdir (dirent change)
 *   7. parent dir's mtime advances after rename
 *
 * To make ordering observable, we sleep 1100ms between operations so
 * the 1-second-resolution mtime field always ticks forward.
 *
 * Assumes the Goldfish RTC returns a wall-clock value that is at least
 * post-2020 (epoch >= 1577836800). QEMU defaults to host time, which
 * any modern host satisfies.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EPOCH_FLOOR 1577836800UL   /* 2020-01-01 UTC */

static int pass = 0, fail = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[timestamp_test] PASS  %s\n", msg); pass++; }
    else      { printf("[timestamp_test] FAIL  %s\n", msg); fail++; }
}

static void try_unlink(const char *p) { (void)unlink(p); }

int main(void) {
    /* Pre-clean from any prior run. */
    try_unlink("/mnt/ts_a");
    try_unlink("/mnt/ts_b");
    try_unlink("/mnt/ts_renamed");
    try_unlink("/mnt/ts_dir");

    /* ---- 1. Newly created file has a non-zero, recent mtime ---- */
    int fd = open("/mnt/ts_a", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /mnt/ts_a");
    if (fd >= 0) close(fd);

    struct stat st1;
    chk(stat("/mnt/ts_a", &st1) == 0, "stat /mnt/ts_a");
    chk(st1.st_mtime > 0, "mtime is non-zero (RTC sourced, not stale 0)");
    chk(st1.st_mtime > EPOCH_FLOOR,
        "mtime is post-2020 epoch (RTC returns real wall-clock)");

    /* ---- 2. Single-timestamp scheme: a/m/c all equal ---- */
    chk(st1.st_atime == st1.st_mtime, "atime == mtime");
    chk(st1.st_mtime == st1.st_ctime, "mtime == ctime");

    /* ---- 3. Write advances mtime ---- */
    sleep_ms(1100);
    fd = open("/mnt/ts_a", O_WRONLY);
    if (fd >= 0) { write(fd, "data", 4); close(fd); }

    struct stat st2;
    stat("/mnt/ts_a", &st2);
    chk(st2.st_mtime > st1.st_mtime, "mtime advanced after write");

    /* ---- 4. Truncate (O_TRUNC re-open) advances mtime ---- */
    sleep_ms(1100);
    fd = open("/mnt/ts_a", O_WRONLY | O_TRUNC);
    if (fd >= 0) close(fd);

    struct stat st3;
    stat("/mnt/ts_a", &st3);
    chk(st3.st_mtime > st2.st_mtime, "mtime advanced after truncate");

    /* ---- 5. Hard link bumps target's mtime (nlink change) ---- */
    sleep_ms(1100);
    int rc = link("/mnt/ts_a", "/mnt/ts_b");
    chk(rc == 0, "link ts_a -> ts_b");

    struct stat st4;
    stat("/mnt/ts_a", &st4);
    chk(st4.st_mtime > st3.st_mtime,
        "target mtime advanced after link (metadata change)");

    /* Both names share the inode → both names see the bumped mtime. */
    struct stat st4b;
    stat("/mnt/ts_b", &st4b);
    chk(st4b.st_mtime == st4.st_mtime,
        "linked alias sees same mtime (shared inode)");

    /* ---- 6. mkdir advances parent dir's mtime ---- */
    struct stat dir1;
    chk(stat("/mnt", &dir1) == 0, "stat /mnt");
    sleep_ms(1100);
    rc = mkdir("/mnt/ts_dir", 0755);
    chk(rc == 0, "mkdir /mnt/ts_dir");

    struct stat dir2;
    stat("/mnt", &dir2);
    chk(dir2.st_mtime > dir1.st_mtime,
        "/mnt mtime advanced after mkdir adds child");

    /* ---- 7. rename advances source-dir mtime ---- */
    struct stat dir3;
    stat("/mnt", &dir3);
    sleep_ms(1100);
    rc = rename("/mnt/ts_a", "/mnt/ts_renamed");
    chk(rc == 0, "rename ts_a -> ts_renamed");

    struct stat dir4;
    stat("/mnt", &dir4);
    chk(dir4.st_mtime > dir3.st_mtime,
        "/mnt mtime advanced after rename (dirent rewrite)");

    /* The renamed file's own mtime should not regress. */
    struct stat st_ren;
    chk(stat("/mnt/ts_renamed", &st_ren) == 0, "stat /mnt/ts_renamed");
    chk(st_ren.st_mtime >= st4.st_mtime,
        "renamed file's own mtime preserved (not zeroed)");

    /* Final cleanup. */
    try_unlink("/mnt/ts_b");
    try_unlink("/mnt/ts_renamed");
    try_unlink("/mnt/ts_dir");

    if (fail == 0) printf("timestamp_test: PASS (%d tests)\n", pass);
    else           printf("timestamp_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
