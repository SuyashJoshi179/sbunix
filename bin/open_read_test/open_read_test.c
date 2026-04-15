// open_read_test: verify lseek SEEK_CUR, SEEK_END, and read-past-EOF behaviour.
//
// /etc/rc is used as the test file (small, known to exist, read-only tarfs).
//
// Tests:
//   1. Open succeeds.
//   2. Read 3 bytes returns 3.
//   3. SEEK_CUR+0 reports current position (3).
//   4. SEEK_CUR+2 advances position to 5.
//   5. SEEK_END+0 returns file size (> 0).
//   6. Read at EOF returns 0.
//   7. SEEK_END-1 positions one byte before EOF.
//   8. Read at that position returns 1 byte.
//   9. SEEK_SET to 0 resets position.
//  10. Re-read 3 bytes returns 3.
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int pass_cnt = 0, fail_cnt = 0;

static void chk(int cond, const char *msg) {
    if (cond) { printf("[open_read_test] PASS %s\n", msg); pass_cnt++; }
    else       { printf("[open_read_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    int fd = open("/etc/rc", O_RDONLY);
    chk(fd >= 0, "open /etc/rc");
    if (fd < 0) { printf("open_read_test: FAIL (cannot open)\n"); return 1; }

    // 1. Read 3 bytes.
    char buf[8];
    long n = read(fd, buf, 3);
    chk(n == 3, "read 3 bytes returns 3");

    // 2. SEEK_CUR+0: query current position.
    long pos = lseek(fd, 0, 1 /* SEEK_CUR */);
    chk(pos == 3, "SEEK_CUR+0 reports position 3");

    // 3. SEEK_CUR+2: advance by 2.
    pos = lseek(fd, 2, 1 /* SEEK_CUR */);
    chk(pos == 5, "SEEK_CUR+2 gives position 5");

    // 4. SEEK_END+0: move to end; result is file size.
    long fsz = lseek(fd, 0, 2 /* SEEK_END */);
    chk(fsz > 5, "SEEK_END+0 gives size > 5");

    // 5. Read at EOF should return 0.
    n = read(fd, buf, 1);
    chk(n == 0, "read at EOF returns 0");

    // 6. SEEK_END-1: one byte before EOF.
    pos = lseek(fd, -1, 2 /* SEEK_END */);
    chk(pos == fsz - 1, "SEEK_END-1 gives size-1");
    n = read(fd, buf, 1);
    chk(n == 1, "read last byte succeeds");

    // 7. SEEK_SET back to 0 and re-read.
    pos = lseek(fd, 0, 0 /* SEEK_SET */);
    chk(pos == 0, "SEEK_SET to 0");
    n = read(fd, buf, 3);
    chk(n == 3, "re-read 3 bytes from start");

    close(fd);

    if (fail_cnt == 0)
        printf("open_read_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("open_read_test: FAIL (%d/%d failed)\n", fail_cnt, pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
