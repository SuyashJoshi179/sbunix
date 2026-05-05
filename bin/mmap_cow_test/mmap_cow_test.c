#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(void) {
    /* Use /etc/rc — same file mmap_smoke_test uses. */
    int fd = open("/etc/rc", O_RDONLY);
    if (fd < 0) { printf("mmap_cow: open failed\n"); return 1; }
    char *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p == (void *)-1) { printf("mmap_cow: mmap failed\n"); return 1; }
    char orig = p[0];

    int pid = fork();
    if (pid == 0) {
        p[0] = orig + 1;            /* CoW write in child */
        if (p[0] != orig + 1) _exit(1);
        _exit(0);
    }
    int st;
    wait(&st);
    if (p[0] != orig) {
        printf("mmap_cow: parent saw child write (FAIL)\n");
        return 1;
    }
    /* Verify file unchanged on disk by re-reading via fd. */
    int fd2 = open("/etc/rc", O_RDONLY);
    char buf[1] = {0};
    read(fd2, buf, 1);
    close(fd2);
    if (buf[0] != orig) {
        printf("mmap_cow: file modified on disk (FAIL)\n");
        return 1;
    }
    munmap(p, 4096);
    close(fd);
    printf("mmap_cow: ok\n");
    return 0;
}
