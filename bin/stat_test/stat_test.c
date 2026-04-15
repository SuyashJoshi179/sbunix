#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(void) {
    int fd = open("/bin/init", O_RDONLY);
    if (fd < 0) {
        printf("stat_test: FAIL open /bin/init: %d\n", fd);
        return 1;
    }

    struct stat st;
    int rc = fstat(fd, &st);
    close(fd);

    if (rc < 0) {
        printf("stat_test: FAIL fstat returned %d\n", rc);
        return 1;
    }

    if (!S_ISREG(st.st_mode)) {
        printf("stat_test: FAIL /bin/init not S_IFREG, mode=0x%x\n", st.st_mode);
        return 1;
    }
    if (st.st_size == 0) {
        printf("stat_test: FAIL /bin/init size is 0\n");
        return 1;
    }

    printf("stat_test: PASS (size=%lu mode=0x%x)\n",
           (unsigned long)st.st_size, st.st_mode);
    return 0;
}
