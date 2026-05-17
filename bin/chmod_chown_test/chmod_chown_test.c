#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* POSIX chmod/chown round-trip on sbfs, tmpfs (fchmod), tarfs (EROFS).
 * Permission enforcement is intentionally absent across this kernel
 * (matches sys_access root-equivalent model), so all sbfs/tmpfs calls
 * succeed regardless of caller uid; only the read-only tarfs path
 * returns EROFS. */
int main(void) {
    const char *p = "/mnt/cmod.tmp";
    (void)unlink(p);

    /* 1. Create on sbfs, baseline stat. */
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: create errno=%d\n", errno); return 1; }
    close(fd);

    struct stat st;
    if (stat(p, &st) < 0) { printf("FAIL: stat0 errno=%d\n", errno); return 1; }
    if (!S_ISREG(st.st_mode)) { printf("FAIL: not regular: %o\n", st.st_mode); return 1; }
    if ((st.st_mode & 07777) != 0644) {
        printf("FAIL: default perms %o want 0644\n", st.st_mode & 07777); return 1;
    }

    /* 2. chmod 0600 — only perm bits change. */
    if (chmod(p, 0600) < 0) { printf("FAIL: chmod errno=%d\n", errno); return 1; }
    if (stat(p, &st) < 0)   { printf("FAIL: stat1 errno=%d\n", errno); return 1; }
    if (!S_ISREG(st.st_mode)) { printf("FAIL: type bits lost: %o\n", st.st_mode); return 1; }
    if ((st.st_mode & 07777) != 0600) {
        printf("FAIL: chmod result %o want 0600\n", st.st_mode & 07777); return 1;
    }

    /* 3. chown(42, 7) — full set. */
    if (chown(p, 42, 7) < 0) { printf("FAIL: chown errno=%d\n", errno); return 1; }
    if (stat(p, &st) < 0)    { printf("FAIL: stat2 errno=%d\n", errno); return 1; }
    if (st.st_uid != 42)     { printf("FAIL: uid=%u want 42\n", st.st_uid); return 1; }
    if (st.st_gid != 7)      { printf("FAIL: gid=%u want 7\n", st.st_gid); return 1; }

    /* 4. chown(-1, 99) — gid only. */
    if (chown(p, (uid_t)-1, 99) < 0) {
        printf("FAIL: chown(-1,99) errno=%d\n", errno); return 1;
    }
    if (stat(p, &st) < 0) { printf("FAIL: stat3 errno=%d\n", errno); return 1; }
    if (st.st_uid != 42)  { printf("FAIL: uid changed when sentinel: %u\n", st.st_uid); return 1; }
    if (st.st_gid != 99)  { printf("FAIL: gid=%u want 99\n", st.st_gid); return 1; }

    /* 5. Persist across reopen — exercise dinode writeback path. */
    fd = open(p, O_RDONLY);
    if (fd < 0) { printf("FAIL: reopen errno=%d\n", errno); return 1; }
    struct stat st2;
    if (fstat(fd, &st2) < 0) { printf("FAIL: fstat errno=%d\n", errno); return 1; }
    close(fd);
    if (st2.st_uid != 42 || st2.st_gid != 99 || (st2.st_mode & 07777) != 0600) {
        printf("FAIL: not persisted: uid=%u gid=%u mode=%o\n",
               st2.st_uid, st2.st_gid, st2.st_mode & 07777);
        return 1;
    }

    /* 5b. fchown round-trip on sbfs. The fd path through sys_fchown
     * shares do_chown_ip with sys_chown, but the dispatch is distinct
     * and worth covering explicitly. */
    fd = open(p, O_RDWR);
    if (fd < 0) { printf("FAIL: reopen for fchown errno=%d\n", errno); return 1; }
    if (fchown(fd, 123, 234) < 0) {
        printf("FAIL: fchown errno=%d\n", errno); close(fd); return 1;
    }
    if (fstat(fd, &st2) < 0) { printf("FAIL: fstat post-fchown errno=%d\n", errno); close(fd); return 1; }
    close(fd);
    if (st2.st_uid != 123 || st2.st_gid != 234) {
        printf("FAIL: fchown not visible: uid=%u gid=%u want 123/234\n",
               st2.st_uid, st2.st_gid);
        return 1;
    }

    /* 5c. chown on a directory — exercises do_chown_ip against a dir
     * inode (different sbfs type-bits path through ialloc/stat). */
    const char *dp = "/mnt/cmod.d";
    (void)unlink(dp);
    if (mkdir(dp, 0755) < 0) { printf("FAIL: mkdir errno=%d\n", errno); return 1; }
    if (chown(dp, 77, 88) < 0) { printf("FAIL: dir chown errno=%d\n", errno); return 1; }
    if (stat(dp, &st) < 0)    { printf("FAIL: dir stat errno=%d\n", errno); return 1; }
    if (!S_ISDIR(st.st_mode)) {
        printf("FAIL: dir type bits lost: mode=%o\n", st.st_mode); return 1;
    }
    if (st.st_uid != 77 || st.st_gid != 88) {
        printf("FAIL: dir chown not visible: uid=%u gid=%u\n", st.st_uid, st.st_gid);
        return 1;
    }
    if (chmod(dp, 0700) < 0) { printf("FAIL: dir chmod errno=%d\n", errno); return 1; }
    if (stat(dp, &st) < 0)   { printf("FAIL: dir stat2 errno=%d\n", errno); return 1; }
    if ((st.st_mode & 07777) != 0700 || !S_ISDIR(st.st_mode)) {
        printf("FAIL: dir chmod result %o (type=%o)\n",
               st.st_mode & 07777, st.st_mode & S_IFMT);
        return 1;
    }
    (void)rmdir(dp);

    /* 6. tarfs EROFS — chmod on /bin/sh must fail. */
    errno = 0;
    if (chmod("/bin/sh", 0644) == 0 || errno != EROFS) {
        printf("FAIL: tarfs chmod errno=%d want EROFS=%d\n", errno, EROFS);
        return 1;
    }

    /* 7. fchmod on tmpfs. */
    const char *tp = "/tmp/cmod.tmp";
    (void)unlink(tp);
    fd = open(tp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: tmpfs create errno=%d\n", errno); return 1; }
    if (fchmod(fd, 0700) < 0) { printf("FAIL: fchmod errno=%d\n", errno); return 1; }
    if (fstat(fd, &st) < 0)   { printf("FAIL: tmpfs fstat errno=%d\n", errno); return 1; }
    if ((st.st_mode & 07777) != 0700) {
        printf("FAIL: tmpfs fchmod result %o want 0700\n", st.st_mode & 07777); return 1;
    }
    /* 7b. tmpfs chown — exercises do_chown_ip on a vnode whose stat reads
     * vnode.uid/gid (no setowner hook). Regression: previously
     * tmpfs_op_stat returned hardcoded zeros, silently masking the write. */
    if (chown(tp, 11, 13) < 0) { printf("FAIL: tmpfs chown errno=%d\n", errno); return 1; }
    if (stat(tp, &st) < 0)     { printf("FAIL: tmpfs stat errno=%d\n", errno); return 1; }
    if (st.st_uid != 11 || st.st_gid != 13) {
        printf("FAIL: tmpfs chown not visible: uid=%u gid=%u\n", st.st_uid, st.st_gid);
        return 1;
    }
    close(fd);
    (void)unlink(tp);

    /* 8. lchown on a symlink — sys_lchown uses lnamei_at (no follow), so this
     * must update the LINK's owner, not the target's. Run on sbfs which
     * supports symlinks and persists uid/gid in the dinode. */
    const char *target = "/mnt/lchown.target";
    const char *link   = "/mnt/lchown.lnk";
    (void)unlink(link);
    (void)unlink(target);
    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: lchown target create errno=%d\n", errno); return 1; }
    close(fd);
    if (chown(target, 100, 100) < 0) {
        printf("FAIL: lchown target chown errno=%d\n", errno); return 1;
    }
    if (symlink(target, link) < 0) {
        printf("FAIL: symlink errno=%d\n", errno); return 1;
    }
    if (lchown(link, 55, 66) < 0) {
        printf("FAIL: lchown errno=%d\n", errno); return 1;
    }
    struct stat lst;
    if (lstat(link, &lst) < 0) { printf("FAIL: lstat errno=%d\n", errno); return 1; }
    if (lst.st_uid != 55 || lst.st_gid != 66) {
        printf("FAIL: lchown link uid=%u gid=%u want 55/66\n", lst.st_uid, lst.st_gid);
        return 1;
    }
    /* Target must be untouched. */
    if (stat(target, &st) < 0) { printf("FAIL: lchown target stat errno=%d\n", errno); return 1; }
    if (st.st_uid != 100 || st.st_gid != 100) {
        printf("FAIL: lchown leaked to target: uid=%u gid=%u\n", st.st_uid, st.st_gid);
        return 1;
    }
    (void)unlink(link);
    (void)unlink(target);

    (void)unlink(p);
    printf("PASS\n");
    return 0;
}
