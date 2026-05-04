#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(void) {
    const char *path = "/data/stress.bin";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("stress: open failed\n"); return 1; }
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'A';
    if (write(fd, buf, 4096) != 4096) {
        printf("stress: write failed\n"); return 1;
    }

    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("stress: mmap failed\n"); return 1; }

    for (int round = 0; round < 32; round++) {
        char ch = 'A' + (round % 26);
        for (int i = 0; i < 4096; i++) buf[i] = ch;
        lseek(fd, 0, 0);
        write(fd, buf, 4096);
        if (m[100] != ch) {
            printf("stress: mmap stale after fd-write (round %d)\n", round);
            return 1;
        }
        m[200] = ch + 1;
        msync(m, 4096, MS_SYNC);
        char c;
        lseek(fd, 200, 0);
        read(fd, &c, 1);
        if (c != ch + 1) {
            printf("stress: fd-read stale after mmap-write (round %d)\n",
                   round);
            return 1;
        }
    }
    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("pagecache_stress: ok\n");
    return 0;
}
