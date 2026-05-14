/*
 * rm — remove files (and empty dirs with -d).
 *
 * Usage: rm [-f] [-d] file...
 *
 * Flags:
 *   -f   silently ignore nonexistent files; suppress error messages
 *   -d   allow removal of empty directories
 *
 * Recursive (-r) is intentionally not implemented yet — needs opendir/
 * readdir to walk subtrees. Add later when we ship it.
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static int report(const char *prog, const char *path) {
    fprintf(stderr, "%s: cannot remove '%s': %s\n",
            prog, path, strerror(errno));
    return -1;
}

int main(int argc, char **argv) {
    int force = 0;
    int allow_dir = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        for (int j = 1; argv[i][j]; j++) {
            char c = argv[i][j];
            if      (c == 'f') force = 1;
            else if (c == 'd') allow_dir = 1;
            else {
                fprintf(stderr, "rm: invalid option '-%c'\n", c);
                fprintf(stderr, "usage: rm [-f] [-d] file...\n");
                return 1;
            }
        }
    }
    if (i >= argc) {
        if (force) return 0;   /* POSIX: rm -f with no args is ok */
        fprintf(stderr, "usage: rm [-f] [-d] file...\n");
        return 1;
    }

    (void)allow_dir;   /* unlink already accepts files; sbfs sys_unlink
                        * handles empty-dir removal via the same syscall. */

    int status = 0;
    for (; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            if (force && errno == ENOENT) continue;
            status = report("rm", argv[i]);
        }
    }
    return status ? 1 : 0;
}
