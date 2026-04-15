#include <stdio.h>
#include <unistd.h>

// Smoke test for sleep_ms: three prints paced by ~100 ms sleeps.
// Pass condition: all three lines printed, no hang, no crash.
int main(void) {
    printf("sleep_test: tick 0\n");
    sleep_ms(100);
    printf("sleep_test: tick 1\n");
    sleep_ms(100);
    printf("sleep_test: tick 2\n");
    sleep_ms(100);
    printf("sleep_test: done\n");
    return 0;
}
