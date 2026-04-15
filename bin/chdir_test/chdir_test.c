#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void) {
    // chdir to /bin, then open a binary by relative path.
    int rc = chdir("/bin");
    if (rc < 0) {
        printf("chdir_test: FAIL chdir /bin: %d\n", rc);
        return 1;
    }

    int fd = open("init", O_RDONLY);
    if (fd < 0) {
        printf("chdir_test: FAIL open relative 'init': %d\n", fd);
        return 1;
    }

    char buf[4];
    long n = read(fd, buf, 4);
    close(fd);

    if (n < 4) {
        printf("chdir_test: FAIL short read: %ld\n", n);
        return 1;
    }

    // First 4 bytes of an ELF are the magic number 0x7f 'E' 'L' 'F'.
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        printf("chdir_test: FAIL bad ELF magic: %02x %02x %02x %02x\n",
               (unsigned char)buf[0], (unsigned char)buf[1],
               (unsigned char)buf[2], (unsigned char)buf[3]);
        return 1;
    }

    printf("chdir_test: PASS\n");
    return 0;
}
