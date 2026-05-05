#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

int main(void) {
    const char *path = "/data/pcache.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("pagecache_test: open failed\n"); return 1; }

    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'A';
    if (write(fd, buf, 4096) != 4096) {
        printf("pagecache_test: write failed\n"); return 1;
    }

    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("pagecache_test: mmap failed\n"); return 1; }

    if (m[100] != 'A') {
        printf("pagecache_test: mmap content mismatch\n"); return 1;
    }

    m[100] = 'Z';
    if (msync(m, 4096, MS_SYNC) != 0) {
        printf("pagecache_test: msync failed\n"); return 1;
    }

    lseek(fd, 100, 0 /* SEEK_SET */);
    char c;
    if (read(fd, &c, 1) != 1 || c != 'Z') {
        printf("pagecache_test: read-via-fd did not see mmap write\n");
        return 1;
    }

    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("pagecache_test: ok\n");
    return 0;
}
