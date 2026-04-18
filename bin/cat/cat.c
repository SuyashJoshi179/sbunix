#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

static void cat_fd(int fd) {
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        cat_fd(0);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: cannot open '%s'\n", argv[i]);
            return 1;
        }
        cat_fd(fd);
        close(fd);
    }
    return 0;
}
