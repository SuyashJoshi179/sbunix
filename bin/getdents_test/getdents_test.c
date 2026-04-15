#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

int main(void) {
    int fd = open("/bin", O_RDONLY);
    if (fd < 0) {
        printf("getdents_test: FAIL open /bin: %d\n", fd);
        return 1;
    }

    char buf[2048];
    int  count = 0;
    long n;

    while ((n = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;
        while (off < n) {
            struct dirent64 *de = (struct dirent64 *)(buf + off);
            printf("  %s (type=%d)\n", de->d_name, (int)de->d_type);
            count++;
            off += de->d_reclen;
        }
    }

    close(fd);

    if (count == 0) {
        printf("getdents_test: FAIL no entries in /bin\n");
        return 1;
    }
    printf("getdents_test: PASS (%d entries)\n", count);
    return 0;
}
