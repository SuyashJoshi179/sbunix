/* mntent_test (T2.20): setmntent/getmntent should surface every mount
 * the kernel actually has, including tmpfs at /tmp. */
#include <mntent.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, label) do { \
    if (!(cond)) { printf("FAIL: %s\n", label); fails++; } \
} while (0)

static int has_entry(const char *fsname, const char *dir, const char *type) {
    FILE *fp = setmntent("/etc/mtab", "r");
    if (!fp) return 0;
    int found = 0;
    struct mntent *m;
    while ((m = getmntent(fp))) {
        if (strcmp(m->mnt_fsname, fsname) == 0
         && strcmp(m->mnt_dir, dir) == 0
         && strcmp(m->mnt_type, type) == 0) {
            found = 1;
            break;
        }
    }
    endmntent(fp);
    return found;
}

int main(void) {
    CHECK(has_entry("tarfs", "/", "tarfs"),  "tarfs / present");
    CHECK(has_entry("sbfs",  "/mnt", "sbfs"), "sbfs /mnt present");
    CHECK(has_entry("tmpfs", "/tmp", "tmpfs"), "tmpfs /tmp present");

    if (fails == 0) printf("mntent_test: PASS\n");
    else printf("mntent_test: %d FAIL(s)\n", fails);
    return fails;
}
