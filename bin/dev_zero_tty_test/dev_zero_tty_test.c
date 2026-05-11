/* dev_zero_tty_test — exercise /dev/zero and /dev/tty (F-B05/F-B06).
 *
 * /dev/zero is a Linux-classic character device that returns NUL bytes on
 * read and discards writes. /dev/tty is the controlling-terminal alias for
 * the console; in SBUnix it routes to the same backend as /dev/console but
 * carries its own (major,minor) = (5,0). */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int fails = 0;

static void ok(int cond, const char *name) {
    if (cond) printf("[dev_zero_tty] PASS  %s\n", name);
    else      { printf("[dev_zero_tty] FAIL  %s\n", name); fails++; }
}

int main(void) {
    /* /dev/zero: open, read 64 bytes, verify all NUL and full count. */
    int zfd = open("/dev/zero", O_RDWR);
    ok(zfd >= 0, "open /dev/zero O_RDWR");
    if (zfd >= 0) {
        char buf[64];
        memset(buf, 0xAB, sizeof(buf));
        long r = read(zfd, buf, sizeof(buf));
        ok(r == (long)sizeof(buf), "/dev/zero read returns full count");
        int all_zero = 1;
        for (unsigned i = 0; i < sizeof(buf); i++) if (buf[i] != 0) { all_zero = 0; break; }
        ok(all_zero, "/dev/zero read filled buffer with NUL");

        long w = write(zfd, "xyz", 3);
        ok(w == 3, "/dev/zero write returns count (discarded)");

        struct stat st;
        int rc = fstat(zfd, &st);
        ok(rc == 0, "fstat /dev/zero");
        ok(S_ISCHR(st.st_mode), "/dev/zero is char-special");
        ok(major(st.st_rdev) == 1 && minor(st.st_rdev) == 5,
           "/dev/zero rdev is (1,5)");
        close(zfd);
    }

    /* /dev/tty: open writable, write a marker, fstat reports char-special. */
    int tfd = open("/dev/tty", O_WRONLY);
    ok(tfd >= 0, "open /dev/tty O_WRONLY");
    if (tfd >= 0) {
        static const char marker[] = "[dev_zero_tty] tty-write-ok\n";
        long w = write(tfd, marker, sizeof(marker) - 1);
        ok(w == (long)(sizeof(marker) - 1), "/dev/tty write returns count");

        struct stat st;
        int rc = fstat(tfd, &st);
        ok(rc == 0, "fstat /dev/tty");
        ok(S_ISCHR(st.st_mode), "/dev/tty is char-special");
        ok(major(st.st_rdev) == 5 && minor(st.st_rdev) == 0,
           "/dev/tty rdev is (5,0)");
        close(tfd);
    }

    printf("=== dev_zero_tty_test: %d failures ===\n", fails);
    return fails ? 1 : 0;
}
