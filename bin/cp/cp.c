/*
 * cp — copy a file.
 *
 * Usage: cp source target
 *
 * Reads source and writes to target; target is created if missing,
 * truncated otherwise. No directory-target support yet (single-pair
 * form only). Recursive (-r) not implemented.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define CP_BUFSZ 1024

static int copy_one(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        printf("cp: cannot open '%s': errno %d\n", src, -sfd);
        return -1;
    }

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (dfd < 0) {
        printf("cp: cannot create '%s': errno %d\n", dst, -dfd);
        close(sfd);
        return -1;
    }

    char buf[CP_BUFSZ];
    long n;
    int rc = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            long w = write(dfd, buf + off, n - off);
            if (w <= 0) {
                printf("cp: short/error write to '%s': %ld\n", dst, w);
                rc = -1;
                goto done;
            }
            off += w;
        }
    }
    if (n < 0) {
        printf("cp: read error on '%s': %ld\n", src, n);
        rc = -1;
    }

done:
    close(sfd);
    close(dfd);
    if (rc < 0) (void)unlink(dst);   /* best-effort cleanup */
    return rc;
}

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        printf("cp: invalid option '%s'\n", argv[i]);
        printf("usage: cp source target\n");
        return 1;
    }

    if (argc - i != 2) {
        printf("usage: cp source target\n");
        return 1;
    }

    return copy_one(argv[i], argv[i + 1]) < 0 ? 1 : 0;
}
