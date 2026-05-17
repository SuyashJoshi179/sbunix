/*
 * touch — create empty file if missing, then stamp atime/mtime to now.
 *
 * Two-step per POSIX: ensure the file exists (open with O_CREAT), then
 * bump timestamps via utimensat(AT_FDCWD, path, NULL, 0). The kernel
 * treats a NULL timespec as "set both to current realtime" and a NULL
 * timespec is also what utime(NULL) / utimes(NULL) shorthand maps to.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: touch file...\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        close(fd);
        if (utimensat(AT_FDCWD, argv[i], 0, 0) < 0) {
            fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}
