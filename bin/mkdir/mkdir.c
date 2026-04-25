#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int report(const char *path, int rc) {
    printf("mkdir: cannot create directory '%s': errno %d\n", path, -rc);
    return -1;
}

static int do_one(const char *path) {
    int rc = mkdir(path, 0755);
    if (rc < 0) return report(path, rc);
    return 0;
}

static int do_parents(const char *path) {
    char buf[256];
    int len = (int)strlen(path);
    if (len == 0) {
        printf("mkdir: empty path\n");
        return -1;
    }
    if (len >= (int)sizeof(buf)) {
        printf("mkdir: path too long: '%s'\n", path);
        return -1;
    }
    memcpy(buf, path, len + 1);

    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';

    for (int i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            int rc = mkdir(buf, 0755);
            if (rc < 0 && rc != -EEXIST) return report(buf, rc);
            buf[i] = '/';
        }
    }
    int rc = mkdir(buf, 0755);
    if (rc < 0 && rc != -EEXIST) return report(buf, rc);
    return 0;
}

int main(int argc, char **argv) {
    int parents = 0;
    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0') break;
        if (argv[i][1] == '-' && argv[i][2] == '\0') { i++; break; }
        if (argv[i][1] == 'p' && argv[i][2] == '\0') {
            parents = 1;
            continue;
        }
        printf("mkdir: invalid option '%s'\n", argv[i]);
        printf("usage: mkdir [-p] dir...\n");
        return 1;
    }
    if (i >= argc) {
        printf("usage: mkdir [-p] dir...\n");
        return 1;
    }
    int status = 0;
    for (; i < argc; i++) {
        int rc = parents ? do_parents(argv[i]) : do_one(argv[i]);
        if (rc < 0) status = 1;
    }
    return status;
}
