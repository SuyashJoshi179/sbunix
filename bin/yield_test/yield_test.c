#include <stdio.h>
#include <unistd.h>

// Two forked processes each yield after every print.
// Pass condition: output alternates p0 c0 p1 c1 ... (cooperative handoff).
int main(void) {
    int pid = fork();
    const char *tag = (pid == 0) ? "c" : "p";
    for (int i = 0; i < 10; i++) {
        printf("%s%d ", tag, i);
        sched_yield();
    }
    printf("\n");
    if (pid > 0) {
        int st;
        wait(&st);
    }
    return 0;
}
