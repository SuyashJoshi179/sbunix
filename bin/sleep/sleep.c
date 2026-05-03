#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: sleep <seconds>\n");
        return 1;
    }
    long secs = 0;
    const char *p = argv[1];
    while (*p >= '0' && *p <= '9') secs = secs * 10 + (*p++ - '0');

    struct timespec req = { .tv_sec = secs, .tv_nsec = 0 };
    struct timespec rem;
    while (nanosleep(&req, &rem) < 0 && errno == EINTR) {
        req = rem;
    }
    return 0;
}
