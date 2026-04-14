// pid_test: verify getpid() and the fork() return-value contract.
//
// Expected output (order of parent/child lines may vary):
//   [pid_test] my pid before fork = N  (N > 0)
//   [pid_test] parent: fork returned child_pid > 0, my pid = N
//   [pid_test] child:  fork returned 0, my pid = N+1  (or next allocated)
//   [pid_test] PASS (printed by each branch independently)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    int my_pid = getpid();
    printf("[pid_test] my pid before fork = %d\n", my_pid);

    if (my_pid <= 0) {
        printf("[pid_test] FAIL: getpid() returned %d\n", my_pid);
        exit(1);
    }

    int rc = fork();

    if (rc < 0) {
        printf("[pid_test] FAIL: fork() returned %d\n", rc);
        exit(1);
    }

    if (rc == 0) {
        // Child branch
        int child_pid = getpid();
        if (child_pid <= 0) {
            printf("[pid_test] FAIL child: getpid() = %d\n", child_pid);
            exit(1);
        }
        if (child_pid == my_pid) {
            printf("[pid_test] FAIL child: same pid as parent (%d)\n", child_pid);
            exit(1);
        }
        printf("[pid_test] PASS child: fork()=0, getpid()=%d (parent was %d)\n",
               child_pid, my_pid);
    } else {
        // Parent branch
        int parent_pid = getpid();
        if (parent_pid != my_pid) {
            printf("[pid_test] FAIL parent: getpid() changed after fork (%d -> %d)\n",
                   my_pid, parent_pid);
            exit(1);
        }
        printf("[pid_test] PASS parent: fork()=%d (child pid), getpid()=%d\n",
               rc, parent_pid);
        wait(0);
    }

    return 0;
}
