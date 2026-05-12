/* scandir_test (T2.19): scandir + alphasort. Create a tmpfs directory
 * with predictable entries, then verify scandir filters with the user
 * callback, sorts via compar, and returns the count. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

static int no_dots(const struct dirent *e) {
    if (e->d_name[0] == '.' && e->d_name[1] == 0) return 0;
    if (e->d_name[0] == '.' && e->d_name[1] == '.' && e->d_name[2] == 0) return 0;
    return 1;
}

static void touch(const char *path) {
    int fd = open(path, O_CREAT | O_WRONLY);
    if (fd >= 0) close(fd);
}

static void free_list(struct dirent **list, int n) {
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
}

int main(void) {
    /* Predictable contents in /tmp (tmpfs). Pre-clean in case of rerun. */
    unlink("/tmp/scandir_d/alpha");
    unlink("/tmp/scandir_d/mid");
    unlink("/tmp/scandir_d/zeta");
    rmdir("/tmp/scandir_d");

    mkdir("/tmp/scandir_d", 0777);
    touch("/tmp/scandir_d/zeta");
    touch("/tmp/scandir_d/alpha");
    touch("/tmp/scandir_d/mid");

    struct dirent **list = 0;

    int n = scandir("/tmp/scandir_d", &list, no_dots, alphasort);
    CHECK(n == 3, "filter+alphasort returns 3");
    if (n == 3) {
        CHECK(strcmp(list[0]->d_name, "alpha") == 0, "sorted[0]=alpha");
        CHECK(strcmp(list[1]->d_name, "mid")   == 0, "sorted[1]=mid");
        CHECK(strcmp(list[2]->d_name, "zeta")  == 0, "sorted[2]=zeta");
    }
    free_list(list, n);

    n = scandir("/tmp/scandir_d", &list, 0, alphasort);
    CHECK(n >= 5, "no filter includes . and ..");
    free_list(list, n);

    n = scandir("/tmp/scandir_d", &list, no_dots, 0);
    CHECK(n == 3, "compar=NULL still returns 3");
    free_list(list, n);

    list = 0;
    int rc = scandir("/no/such/dir", &list, 0, alphasort);
    CHECK(rc == -1, "missing dirp returns -1");

    unlink("/tmp/scandir_d/alpha");
    unlink("/tmp/scandir_d/mid");
    unlink("/tmp/scandir_d/zeta");
    rmdir("/tmp/scandir_d");

    if (fails == 0) printf("scandir_test: PASS\n");
    else printf("scandir_test: %d FAIL(s)\n", fails);
    return fails;
}
