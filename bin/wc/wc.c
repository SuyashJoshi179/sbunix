#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int fd = 0;
    if (argc > 1) {
        fd = open(argv[1], 0);
        if (fd < 0) {
            fprintf(stderr, "wc: %s: %s\n", argv[1], strerror(errno));
            return 1;
        }
    }

    long lines = 0, words = 0, chars = 0;
    int in_word = 0;
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            chars++;
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    printf("%ld %ld %ld\n", lines, words, chars);
    if (fd != 0) close(fd);
    return 0;
}
