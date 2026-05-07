#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>

int main(void) {
    int pass = 0, fail = 0;
    struct rlimit r;

    if (getrlimit(RLIMIT_STACK, &r) != 0) { fail++; printf("FAIL getrlimit stack\n"); }
    else if (r.rlim_cur != 8UL * 1024 * 1024 || r.rlim_max != 64UL * 1024 * 1024) {
        printf("FAIL stack default cur=%lu max=%lu\n", r.rlim_cur, r.rlim_max); fail++;
    } else { pass++; printf("PASS stack default 8M/64M\n"); }

    if (getrlimit(RLIMIT_NOFILE, &r) != 0) { fail++; printf("FAIL getrlimit nofile\n"); }
    else if (r.rlim_cur != 16 || r.rlim_max != 64) {
        printf("FAIL nofile default cur=%lu max=%lu\n", r.rlim_cur, r.rlim_max); fail++;
    } else { pass++; printf("PASS nofile default 16/64\n"); }

    r.rlim_cur = 16UL * 1024 * 1024;
    r.rlim_max = 64UL * 1024 * 1024;
    if (setrlimit(RLIMIT_STACK, &r) != 0) { fail++; printf("FAIL setrlimit raise\n"); }
    else { pass++; printf("PASS setrlimit raise\n"); }

    r.rlim_cur = 128UL * 1024 * 1024;
    r.rlim_max = 64UL  * 1024 * 1024;
    if (setrlimit(RLIMIT_STACK, &r) == 0 || errno != EINVAL) {
        fail++; printf("FAIL setrlimit soft>hard\n");
    } else { pass++; printf("PASS setrlimit soft>hard EINVAL\n"); }

    if (getrlimit(99, &r) == 0 || errno != EINVAL) {
        fail++; printf("FAIL bad resource\n");
    } else { pass++; printf("PASS bad resource EINVAL\n"); }

    printf("setrlimit_test: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
