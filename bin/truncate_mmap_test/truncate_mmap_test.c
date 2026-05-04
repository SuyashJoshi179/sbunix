#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(void) {
    const char *path = "/data/trunc_mmap.txt";
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) { printf("truncate_mmap: open failed\n"); return 1; }
    char buf[8192];
    for (int i = 0; i < 8192; i++) buf[i] = 'A';
    if (write(fd, buf, 8192) != 8192) {
        printf("truncate_mmap: write failed\n"); return 1;
    }

    char *m = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { printf("truncate_mmap: mmap failed\n"); return 1; }

    /* Touch second page so it gets mapped via fault path. */
    volatile char c = m[5000];
    (void)c;

    /* Truncate via re-open with O_TRUNC. */
    int fd2 = open(path, O_RDWR | O_TRUNC);
    if (fd2 < 0) { printf("truncate_mmap: trunc open failed\n"); return 1; }
    close(fd2);

    int pid = fork();
    if (pid == 0) {
        volatile char x = m[5000];   /* should SIGBUS / SIGSEGV — past EOF */
        (void)x;
        _exit(0);                    /* kernel allowed past-EOF read */
    }
    int st;
    wait(&st);
    munmap(m, 8192);
    close(fd);
    unlink(path);
    if (WIFSIGNALED(st)) {
        printf("truncate_mmap: ok (child killed by signal %d)\n",
               WTERMSIG(st));
        return 0;
    }
    printf("truncate_mmap: child exited normally — past-EOF allowed\n");
    return 1;
}
