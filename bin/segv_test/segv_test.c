#include <stdio.h>
#include <unistd.h>

// The child dereferences a NULL pointer.  The kernel must kill only
// the child; the parent must survive, wait successfully, and print the
// result.  The kernel must NOT panic.
int main(void) {
    int pid = fork();
    if (pid == 0) {
        printf("segv_test child: about to deref NULL\n");
        *(volatile int *)0 = 0xdead;   // store to unmapped page → fault
        printf("segv_test child: SHOULD NOT REACH HERE\n");
        return 99;
    }
    int st = -1;
    int r = wait(&st);
    printf("segv_test parent: child pid=%d exited status=%d (expected non-zero)\n",
           r, st);
    return (st != 0) ? 0 : 1;  // pass if child exited with non-zero status
}
