#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* OPTS exit-code conventions (posixtest.h):
 *   0 = PASS, 1 = FAIL, 2 = UNRESOLVED, 4 = UNSUPPORTED, 5 = UNTESTED.
 * Any other non-zero status is treated as FAIL. */
enum { PTS_PASS = 0, PTS_FAIL = 1, PTS_UNRESOLVED = 2,
       PTS_UNSUPPORTED = 4, PTS_UNTESTED = 5 };

static unsigned long pass, fail, unresolved, unsupported, untested, other;

static const char *label(int status) {
    if (!WIFEXITED(status)) return "FAIL  (signaled)";
    switch (WEXITSTATUS(status)) {
        case PTS_PASS:        pass++;        return "PASS";
        case PTS_FAIL:        fail++;        return "FAIL";
        case PTS_UNRESOLVED:  unresolved++;  return "UNRESOLVED";
        case PTS_UNSUPPORTED: unsupported++; return "UNSUPPORTED";
        case PTS_UNTESTED:    untested++;    return "UNTESTED";
        default:              other++;       return "FAIL  (other)";
    }
}

static void run_one(const char *path) {
    int pid = fork();
    if (pid == 0) {
        char *const argv[] = { (char *)path, NULL };
        execv(path, argv);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    printf("%-40s %s\n", path, label(status));
}

static void walk(const char *dir) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "opts_run: cannot open %s\n", dir);
        return;
    }
    /* getdents-style scan via libc dirent. */
    DIR *d = fdopendir(fd);
    if (!d) { close(fd); return; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[256];
        int n = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (n <= 0 || n >= (int)sizeof path) continue;
        struct stat st;
        if (stat(path, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk(path);
        } else if (S_ISREG(st.st_mode)) {
            run_one(path);
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    const char *root = (argc > 1) ? argv[1] : "/bin/optsbin";
    printf("=== opts_run %s ===\n", root);
    walk(root);
    printf("=== Totals ===\n");
    printf("PASS:        %lu\n", pass);
    printf("FAIL:        %lu\n", fail);
    printf("UNRESOLVED:  %lu\n", unresolved);
    printf("UNSUPPORTED: %lu\n", unsupported);
    printf("UNTESTED:    %lu\n", untested);
    printf("OTHER:       %lu\n", other);
    return (fail == 0 && other == 0) ? 0 : 1;
}
