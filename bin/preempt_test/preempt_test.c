#include <stdio.h>
#include <unistd.h>

// Two forked processes run tight loops — no explicit yield between
// iterations.  If preemption works they interleave; if not, one
// finishes before the other starts.
int main(void) {
    int pid = fork();
    const char *tag = (pid == 0) ? "child" : "parent";
    for (int i = 0; i < 8; i++) {
        // Tight spin to consume a few timer ticks.
        for (volatile int j = 0; j < 300000; j++) { }
        printf("%s tick %d\n", tag, i);
    }
    if (pid > 0) {
        int st;
        wait(&st);
    }
    return 0;
}
