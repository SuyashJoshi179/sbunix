#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    const char *path = "/data/bigfile.bin";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("bigfile: open failed\n"); return 1; }
    /* Cache pool = 256 KB. Use 512 KB to force eviction. */
    char buf[4096];
    for (int p = 0; p < 128; p++) {
        for (int i = 0; i < 4096; i++) buf[i] = (char)(p ^ i);
        if (write(fd, buf, 4096) != 4096) {
            printf("bigfile: write %d failed\n", p); return 1;
        }
    }
    lseek(fd, 0, 0);
    for (int p = 0; p < 128; p++) {
        if (read(fd, buf, 4096) != 4096) {
            printf("bigfile: read %d failed\n", p); return 1;
        }
        for (int i = 0; i < 4096; i++) {
            if (buf[i] != (char)(p ^ i)) {
                printf("bigfile: mismatch p=%d i=%d\n", p, i);
                return 1;
            }
        }
    }
    close(fd);
    unlink(path);
    printf("bigfile_pcache: ok\n");
    return 0;
}
