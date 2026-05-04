#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(void) {
    /* Pick a known-present small file in tarfs. */
    int fd = open("/etc/rc", O_RDONLY);
    if (fd < 0) { printf("mmap_smoke: open failed\n"); return 1; }
    char *p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == (void *)-1) { printf("mmap_smoke: mmap failed\n"); return 1; }
    char c = p[0];
    printf("mmap_smoke: first byte ok (%d)\n", (int)c);
    munmap(p, 4096);
    close(fd);
    return 0;
}
