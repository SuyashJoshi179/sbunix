#include <stdio.h>
#include <stdlib.h>

static int fails = 0;

static void check(int cond, const char *name) {
    if (cond) {
        printf("[stack_test] PASS  %s\n", name);
    } else {
        printf("[stack_test] FAIL  %s\n", name);
        fails++;
    }
}

static volatile int depth_reached = 0;

static void recurse(int depth) {
    volatile char buf[512];
    buf[0] = (char)depth;
    buf[511] = (char)depth;
    (void)buf;
    if (depth >= 100) {
        depth_reached = depth;
        return;
    }
    recurse(depth + 1);
}

int main(void) {
    printf("=== stack_test ===\n");

    recurse(0);
    check(depth_reached == 100, "deep recursion (100 levels x 512B) succeeded");

    printf("=== stack_test: %d failures ===\n", fails);
    return fails;
}
