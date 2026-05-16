/*
 * wc — count lines, words, characters.
 *
 * Usage:
 *   wc [-l] [file]
 *
 * Default output is "lines words chars" (POSIX -lwc). With -l, only the
 * line count is printed. Reads stdin when no file is given.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int lflag = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        if (argv[i][1] == 'l' && argv[i][2] == '\0') {
            lflag = 1;
            continue;
        }
        fprintf(stderr, "wc: invalid option '%s'\n", argv[i]);
        fprintf(stderr, "usage: wc [-l] [file]\n");
        return 1;
    }

    int fd = 0;
    if (i < argc) {
        fd = open(argv[i], 0);
        if (fd < 0) {
            fprintf(stderr, "wc: %s: %s\n", argv[i], strerror(errno));
            return 1;
        }
    }

    long lines = 0, words = 0, chars = 0;
    int in_word = 0;
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long j = 0; j < n; j++) {
            chars++;
            if (buf[j] == '\n') lines++;
            if (buf[j] == ' ' || buf[j] == '\t' || buf[j] == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    if (lflag) printf("%ld\n", lines);
    else       printf("%ld %ld %ld\n", lines, words, chars);
    if (fd != 0) close(fd);
    return 0;
}
