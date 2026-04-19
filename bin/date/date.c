#include <stdio.h>
#include <time.h>

int main(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        printf("date: clock_gettime failed\n");
        return 1;
    }
    long ms = (long)(ts.tv_nsec / 1000000);
    printf("up %lld.%03ld s\n", (long long)ts.tv_sec, ms);
    return 0;
}
