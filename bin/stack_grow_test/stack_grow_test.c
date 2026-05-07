#include <stdio.h>
#include <sys/resource.h>

static volatile int sink;

static void recurse(int depth, int max_depth) {
    volatile char buf[1024];
    buf[0] = (char)depth;
    sink ^= buf[0];
    if (depth + 1 < max_depth) recurse(depth + 1, max_depth);
}

int main(void) {
    struct rlimit r;
    getrlimit(RLIMIT_STACK, &r);
    printf("stack soft=%lu max=%lu\n", r.rlim_cur, r.rlim_max);

    recurse(0, 4096);
    printf("PASS stack_grow 4MiB recursion under default 8MiB soft\n");
    return 0;
}
