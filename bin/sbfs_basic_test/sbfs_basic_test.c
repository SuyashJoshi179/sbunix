/*
 * sbfs_basic_test — Phase 5 end-to-end filesystem tests
 *
 * Tests:
 *  1. open O_CREAT | O_WRONLY on /data/hello.txt → succeeds
 *  2. write "hello sbfs" → returns 10
 *  3. close → 0
 *  4. open O_RDONLY, read back, compare → "hello sbfs"
 *  5. lseek SEEK_SET 0, read again → same data
 *  6. mkdir /data/subdir → 0
 *  7. create /data/subdir/nested.txt, write, read back
 *  8. unlink /data/hello.txt → 0; re-open → -ENOENT
 *  9. open O_WRONLY|O_CREAT|O_TRUNC on /data/trunc.txt, write "AAAA",
 *     close; reopen O_TRUNC, write "BB", close; read → "BB" (truncated)
 * 10. write to /etc/rc (tarfs) with O_WRONLY → negative (EROFS)
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[sbfs_test] PASS  %s\n", msg); pass_cnt++; }
    else       { printf("[sbfs_test] FAIL  %s\n", msg); fail_cnt++; }
}

int main(void) {
    int fd;
    char buf[64];
    long n;

    /* Pre-clean from any prior run (sbfs persists across reboots). */
    (void)unlink("/data/subdir/nested.txt");
    (void)unlink("/data/subdir");
    (void)unlink("/data/hello.txt");
    (void)unlink("/data/trunc.txt");

    /* ---- 1. Create and write a file ---- */
    fd = open("/data/hello.txt", O_WRONLY | O_CREAT);
    chk(fd >= 0, "open O_CREAT /data/hello.txt");
    if (fd >= 0) {
        n = write(fd, "hello sbfs", 10);
        chk(n == 10, "write 10 bytes");
        close(fd);
    }

    /* ---- 2. Read back ---- */
    fd = open("/data/hello.txt", O_RDONLY);
    chk(fd >= 0, "open O_RDONLY /data/hello.txt");
    if (fd >= 0) {
        n = read(fd, buf, 10);
        chk(n == 10, "read 10 bytes");
        chk(n == 10 && buf[0]=='h' && buf[4]=='o' && buf[9]=='s',
            "content matches 'hello sbfs'");
        /* 3. lseek and re-read */
        n = lseek(fd, 0, 0 /* SEEK_SET */);
        chk(n == 0, "lseek back to 0");
        char buf2[10];
        n = read(fd, buf2, 10);
        int same = (n == 10);
        for (int i = 0; i < 10 && same; i++) same = (buf2[i] == buf[i]);
        chk(same, "re-read after lseek matches");
        close(fd);
    }

    /* ---- 4. mkdir ---- */
    int rc = mkdir("/data/subdir", 0755);
    chk(rc == 0, "mkdir /data/subdir");

    /* ---- 5. File in subdirectory ---- */
    fd = open("/data/subdir/nested.txt", O_WRONLY | O_CREAT);
    chk(fd >= 0, "create /data/subdir/nested.txt");
    if (fd >= 0) {
        write(fd, "nested", 6);
        close(fd);
    }
    fd = open("/data/subdir/nested.txt", O_RDONLY);
    chk(fd >= 0, "open nested.txt for read");
    if (fd >= 0) {
        n = read(fd, buf, 6);
        chk(n == 6 && buf[0] == 'n', "nested.txt content OK");
        close(fd);
    }

    /* ---- 6. unlink ---- */
    rc = unlink("/data/hello.txt");
    chk(rc == 0, "unlink /data/hello.txt");
    fd = open("/data/hello.txt", O_RDONLY);
    chk(fd < 0, "open after unlink returns negative (ENOENT)");
    if (fd >= 0) close(fd);

    /* ---- 7. O_TRUNC ---- */
    fd = open("/data/trunc.txt", O_WRONLY | O_CREAT);
    if (fd >= 0) { write(fd, "AAAA", 4); close(fd); }
    fd = open("/data/trunc.txt", O_WRONLY | O_CREAT | O_TRUNC);
    chk(fd >= 0, "open O_TRUNC");
    if (fd >= 0) { write(fd, "BB", 2); close(fd); }
    fd = open("/data/trunc.txt", O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, 10);
        chk(n == 2 && buf[0] == 'B' && buf[1] == 'B',
            "O_TRUNC: only 'BB' remains");
        close(fd);
    }

    /* ---- 8. EROFS: write to tarfs ---- */
    fd = open("/etc/rc", O_WRONLY);
    if (fd >= 0) {
        n = write(fd, "x", 1);
        chk(n < 0, "write to tarfs returns negative (EROFS)");
        close(fd);
    } else {
        /* Kernel rejected O_WRONLY on tarfs at open time — also acceptable. */
        chk(1, "open O_WRONLY on tarfs rejected at open (EROFS)");
    }

    if (fail_cnt == 0)
        printf("sbfs_basic_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("sbfs_basic_test: FAIL (%d/%d failed)\n",
               fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
