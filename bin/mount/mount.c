#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>

static void usage(void) {
    fprintf(stderr, "usage: mount -t TYPE [SOURCE] TARGET\n");
}

int main(int argc, char **argv) {
    /* Accept "mount -t TYPE SOURCE TARGET" (prof's rc form, 4 args
     * past argv[0]) and "mount -t TYPE TARGET" (3 args). SOURCE is
     * ignored — fstype alone selects the backing. */
    if (argc < 4 || strcmp(argv[1], "-t") != 0) {
        usage();
        return 2;
    }
    const char *fstype = argv[2];
    const char *target = argv[argc - 1];

    if (mount(target, fstype) < 0) {
        fprintf(stderr, "mount: %s on %s failed: %s\n",
                fstype, target, strerror(errno));
        return 1;
    }
    return 0;
}
