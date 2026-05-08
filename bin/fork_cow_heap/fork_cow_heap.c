#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* Heap allocations should be CoW across fork: a child's mutation must
 * not be visible in the parent. */

#define SZ 8192

int main(void) {
    char *p = (char *)malloc(SZ);
    if (!p) { printf("FAIL malloc\n"); return 1; }
    memset(p, 'P', SZ);

    int pid = fork();
    if (pid < 0) { printf("FAIL fork\n"); return 1; }
    if (pid == 0) {
        memset(p, 'C', SZ);
        for (int i = 0; i < SZ; i++) {
            if (p[i] != 'C') {
                printf("FAIL child not seeing own write i=%d\n", i);
                exit(1);
            }
        }
        exit(0);
    }
    int st = 0;
    while (wait(&st) != pid) ;
    if (st != 0) { printf("FAIL child status %d\n", st); return 1; }

    for (int i = 0; i < SZ; i++) {
        if (p[i] != 'P') {
            printf("FAIL parent corrupted i=%d got=%c\n", i, p[i]);
            return 1;
        }
    }
    free(p);
    printf("PASS fork_cow_heap\n");
    return 0;
}
