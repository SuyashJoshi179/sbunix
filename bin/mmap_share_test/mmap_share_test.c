#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(void) {
    const char *path = "/data/share.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("mmap_share: open failed\n"); return 1; }
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'X';
    if (write(fd, buf, 4096) != 4096) {
        printf("mmap_share: write failed\n"); return 1;
    }

    char *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("mmap_share: mmap failed\n"); return 1; }

    int pid = fork();
    if (pid == 0) {
        if (m[0] != 'X') _exit(2);
        m[0] = 'Y';
        _exit(0);
    }
    int st;
    wait(&st);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("mmap_share: child status bad\n"); return 1;
    }
    if (m[0] != 'Y') {
        printf("mmap_share: parent did not see child write\n"); return 1;
    }
    munmap(m, 4096);
    close(fd);
    unlink(path);
    printf("mmap_share: ok\n");
    return 0;
}
