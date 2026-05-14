/*
 * mv — move or rename a file.
 *
 * Usage: mv source target
 *
 * Wraps the kernel's sys_rename, which handles same-directory rename,
 * cross-directory move, and atomic replacement. Multi-source form
 * (mv src... dir) is not implemented yet — pass exactly two arguments.
 *
 * Cross-filesystem moves return -EXDEV from the kernel; mv reports the
 * error rather than falling back to copy+unlink (cp is a separate tool
 * for that workflow).
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        fprintf(stderr, "mv: invalid option '%s'\n", argv[i]);
        fprintf(stderr, "usage: mv source target\n");
        return 1;
    }

    if (argc - i != 2) {
        fprintf(stderr, "usage: mv source target\n");
        return 1;
    }

    if (rename(argv[i], argv[i + 1]) < 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n",
                argv[i], argv[i + 1], strerror(errno));
        return 1;
    }
    return 0;
}
