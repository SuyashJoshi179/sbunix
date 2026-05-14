// tarfs_dots_test: a tarfs directory must expose "." and ".." via getdents.
//
// /bin is served entirely by tarfs. libc readdir() forwards raw getdents64
// records, so a missing "."/".." here means the kernel's tarfs_getdents
// omitted them.
#include <dirent.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int pass = 0, fail = 0;

    DIR *d = opendir("/bin");
    if (!d) {
        printf("[tarfs_dots_test] FAIL opendir(/bin)\n");
        printf("tarfs_dots_test: FAIL (1/1 failed)\n");
        return 1;
    }

    int saw_dot = 0, saw_dotdot = 0, total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        total++;
        if (strcmp(e->d_name, ".")  == 0) saw_dot++;
        if (strcmp(e->d_name, "..") == 0) saw_dotdot++;
    }
    closedir(d);

    if (saw_dot == 1) { printf("[tarfs_dots_test] PASS '.' present exactly once\n"); pass++; }
    else { printf("[tarfs_dots_test] FAIL '.' count=%d\n", saw_dot); fail++; }

    if (saw_dotdot == 1) { printf("[tarfs_dots_test] PASS '..' present exactly once\n"); pass++; }
    else { printf("[tarfs_dots_test] FAIL '..' count=%d\n", saw_dotdot); fail++; }

    if (fail == 0)
        printf("tarfs_dots_test: PASS (%d entries scanned, %d checks)\n", total, pass);
    else
        printf("tarfs_dots_test: FAIL (%d/%d failed)\n", fail, pass + fail);
    return fail ? 1 : 0;
}
