/*
 * ln — create a hard link.
 *
 * Usage: ln source target
 *
 * Symbolic link (-s) is not yet supported by the kernel; we reject -s
 * with an error rather than silently making a hard link. Multi-source
 * form (ln src... dir) is also not implemented yet — pass exactly two
 * arguments.
 */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        if (argv[i][1] == 's' && argv[i][2] == '\0') {
            printf("ln: -s (symbolic link) not supported\n");
            return 1;
        }
        printf("ln: invalid option '%s'\n", argv[i]);
        printf("usage: ln source target\n");
        return 1;
    }

    if (argc - i != 2) {
        printf("usage: ln source target\n");
        return 1;
    }

    int rc = link(argv[i], argv[i + 1]);
    if (rc < 0) {
        printf("ln: cannot link '%s' -> '%s': errno %d\n",
               argv[i], argv[i + 1], -rc);
        return 1;
    }
    return 0;
}
