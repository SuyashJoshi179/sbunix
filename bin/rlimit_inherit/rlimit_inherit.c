#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>

/* Child inherits the parent's rlimits across fork. */

int main(void) {
    struct rlimit r = { 16UL * 1024 * 1024, 32UL * 1024 * 1024 };
    if (setrlimit(RLIMIT_STACK, &r) != 0) {
        printf("FAIL setrlimit parent\n"); return 1;
    }
    int pid = fork();
    if (pid < 0) { printf("FAIL fork\n"); return 1; }
    if (pid == 0) {
        struct rlimit got = { 0, 0 };
        if (getrlimit(RLIMIT_STACK, &got) != 0) {
            printf("FAIL child getrlimit\n"); exit(1);
        }
        if (got.rlim_cur != 16UL * 1024 * 1024 ||
            got.rlim_max != 32UL * 1024 * 1024) {
            printf("FAIL child rlim cur=%lu max=%lu\n",
                   got.rlim_cur, got.rlim_max);
            exit(1);
        }
        exit(0);
    }
    int st = 0;
    while (wait(&st) != pid) ;
    if (st != 0) { printf("FAIL child status %d\n", st); return 1; }
    printf("PASS rlimit_inherit\n");
    return 0;
}
