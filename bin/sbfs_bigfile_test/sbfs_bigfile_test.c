#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* sbfs v3 regression: exercise the double-indirect path.
 *
 *   Layout: direct covers bytes 0 .. 7*512-1 = 3.5 KiB.
 *           single-indirect covers next 2*128*512 = 128 KiB.
 *           double-indirect kicks in at byte 131584 (~129 KiB).
 *
 * We write a 200 KiB pseudo-random pattern (deterministic seed, no libc
 * rand dependency) so the file straddles all three regions and the
 * double-indirect branch is unambiguously exercised. Round-trip
 * verification is byte-for-byte. Then we unlink and confirm the
 * itrunc-via-unlink path doesn't leak via a follow-up 100 KiB write to
 * the same path — if the previous run's blocks weren't freed,
 * a 4000-block fs would ENOSPC quickly when this test runs in sequence
 * after other big-file tests. */
#define SIZE      (200u * 1024u)
#define CHUNK     4096

static unsigned next_rand(unsigned x) { return x * 1103515245u + 12345u; }

int main(void) {
    const char *path = "/mnt/bigfile.bin";
    (void)unlink(path);

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: open errno=%d\n", errno); return 1; }

    char buf[CHUNK];
    unsigned seed = 0xdeadbeef;
    unsigned written = 0;
    while (written < SIZE) {
        for (int i = 0; i < CHUNK; i++) {
            seed = next_rand(seed);
            buf[i] = (char)(seed >> 16);
        }
        int n = write(fd, buf, CHUNK);
        if (n != CHUNK) {
            printf("FAIL: write at %u returned %d errno=%d\n", written, n, errno);
            return 1;
        }
        written += CHUNK;
    }

    if (lseek(fd, 0, SEEK_SET) != 0) {
        printf("FAIL: lseek errno=%d\n", errno); return 1;
    }

    seed = 0xdeadbeef;
    unsigned read_total = 0;
    while (read_total < SIZE) {
        int n = read(fd, buf, CHUNK);
        if (n != CHUNK) {
            printf("FAIL: read at %u returned %d errno=%d\n", read_total, n, errno);
            return 1;
        }
        for (int i = 0; i < CHUNK; i++) {
            seed = next_rand(seed);
            char want = (char)(seed >> 16);
            if (buf[i] != want) {
                printf("FAIL: mismatch at %u: got %02x want %02x\n",
                       read_total + i, (unsigned char)buf[i], (unsigned char)want);
                return 1;
            }
        }
        read_total += CHUNK;
    }
    close(fd);

    /* Truncate-via-unlink test: rewrite a smaller file at the same path.
     * If the previous itrunc didn't free the double-indirect tree, the fs
     * is now 200 KiB heavier; a 100 KiB second pass would still fit but a
     * pathological loop would not. Smoke test for one cycle. */
    if (unlink(path) < 0) { printf("FAIL: unlink errno=%d\n", errno); return 1; }

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: reopen errno=%d\n", errno); return 1; }
    for (int p = 0; p < 25; p++) {        /* 25 * 4096 = 100 KiB */
        memset(buf, (char)p, CHUNK);
        if (write(fd, buf, CHUNK) != CHUNK) {
            printf("FAIL: second-pass write %d errno=%d\n", p, errno); return 1;
        }
    }
    close(fd);
    (void)unlink(path);

    /* Phase 3: single-buffer write that straddles direct → single-indir →
     * double-indir in ONE syscall. Stresses the per-transaction chunking
     * inside sbfs_op_write: if the txn slab were the whole user buffer the
     * log header would overflow (worst case ~5 unique blocks per BSIZE
     * before dedup → BIG > LOG_HDR_MAX=15). 160 KiB lands ~30 KiB into the
     * DI region; static buffer to keep stack small. */
    static char big[160 * 1024];
    for (unsigned i = 0; i < sizeof(big); i++) big[i] = (char)((i * 31u) ^ 0x5a);

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("FAIL: phase3 open errno=%d\n", errno); return 1; }
    int wn = write(fd, big, sizeof(big));
    if (wn != (int)sizeof(big)) {
        printf("FAIL: phase3 single-write returned %d errno=%d\n", wn, errno);
        return 1;
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
        printf("FAIL: phase3 lseek errno=%d\n", errno); return 1;
    }
    static char vbuf[160 * 1024];
    int rn = read(fd, vbuf, sizeof(vbuf));
    if (rn != (int)sizeof(vbuf)) {
        printf("FAIL: phase3 read returned %d errno=%d\n", rn, errno); return 1;
    }
    for (unsigned i = 0; i < sizeof(big); i++) {
        if (vbuf[i] != big[i]) {
            printf("FAIL: phase3 mismatch at %u\n", i); return 1;
        }
    }
    close(fd);
    (void)unlink(path);

    printf("PASS\n");
    return 0;
}
