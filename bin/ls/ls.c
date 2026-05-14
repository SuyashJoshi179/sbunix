#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

struct linux_dirent64 {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n",
                path, strerror(errno));
        return 1;
    }

    char buf[512];
    long n;
    while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(buf + off);
            printf("%s\n", de->d_name);
            off += de->d_reclen;
        }
    }
    close(fd);
    return 0;
}
