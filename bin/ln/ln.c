/*
 * ln — create a link.
 *
 * Usage:
 *   ln        source target      (hard link, link(2))
 *   ln -s     target linkname    (symbolic link, symlink(2))
 *
 * Multi-source form (ln src... dir) is not implemented — pass
 * exactly two non-option arguments.
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int sflag = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        if (argv[i][1] == 's' && argv[i][2] == '\0') {
            sflag = 1;
            continue;
        }
        fprintf(stderr, "ln: invalid option '%s'\n", argv[i]);
        fprintf(stderr, "usage: ln [-s] source target\n");
        return 1;
    }

    if (argc - i != 2) {
        fprintf(stderr, "usage: ln [-s] source target\n");
        return 1;
    }

    if (sflag) {
        if (symlink(argv[i], argv[i + 1]) < 0) {
            fprintf(stderr, "ln: failed to create symlink '%s' -> '%s': %s\n",
                    argv[i + 1], argv[i], strerror(errno));
            return 1;
        }
    } else {
        if (link(argv[i], argv[i + 1]) < 0) {
            fprintf(stderr, "ln: failed to create hard link '%s' => '%s': %s\n",
                    argv[i + 1], argv[i], strerror(errno));
            return 1;
        }
    }
    return 0;
}
