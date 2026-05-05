#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

// Open /etc/rc, read its contents, verify the magic string is present.
int main(void) {
    int fd = open("/etc/rc", O_RDONLY);
    if (fd < 0) {
        printf("fd_test: FAIL open /etc/rc returned %d\n", fd);
        return 1;
    }

    char buf[256];
    long n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("fd_test: FAIL read returned %ld\n", n);
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    close(fd);

    // Look for known magic string in /etc/rc.
    int found = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == 'm' && buf[i+1] == 'o' && buf[i+2] == 'u' && buf[i+3] == 'n' &&
            buf[i+4] == 't') {
            found = 1; break;
        }
    }
    if (!found) {
        printf("fd_test: FAIL magic string not found in /etc/rc\n");
        return 1;
    }

    // Verify lseek: seek to 0, re-read first byte.
    fd = open("/etc/rc", O_RDONLY);
    if (fd < 0) { printf("fd_test: FAIL reopen\n"); return 1; }
    lseek(fd, 0, 0 /*SEEK_SET*/);
    char c;
    if (read(fd, &c, 1) != 1) { printf("fd_test: FAIL re-read\n"); close(fd); return 1; }
    close(fd);

    printf("fd_test: PASS\n");
    return 0;
}
