#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    errno = 0;
    int fd = open("/no/such/path", O_RDONLY);
    printf("open ret=%d errno=%d (expect -1, ENOENT=2)\n", fd, errno);

    errno = 0;
    int r = close(99);
    printf("close ret=%d errno=%d (expect -1, EBADF=9)\n", r, errno);

    errno = 0;
    long w = write(99, "x", 1);
    printf("write ret=%ld errno=%d (expect -1, EBADF=9)\n", w, errno);
    return 0;
}
