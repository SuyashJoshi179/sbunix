// multi_fork_test: fork twice from the same parent and verify that each child
// gets a unique PID different from the parent and from each other.
//
// Expected output (child lines may interleave):
//   [multi_fork] parent pid = P
//   [multi_fork] child1 pid = C1  (C1 != P)
//   [multi_fork] child2 pid = C2  (C2 != P, C2 != C1)
//   [multi_fork] PASS parent: two distinct child pids
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    int parent_pid = getpid();
    printf("[multi_fork] parent pid = %d\n", parent_pid);

    // --- first fork ---
    int pid1 = fork();
    if (pid1 < 0) {
        printf("[multi_fork] FAIL: first fork() returned %d\n", pid1);
        exit(1);
    }
    if (pid1 == 0) {
        int my_pid = getpid();
        if (my_pid == parent_pid) {
            printf("[multi_fork] FAIL child1: same pid as parent (%d)\n", my_pid);
            exit(1);
        }
        printf("[multi_fork] PASS child1: pid = %d\n", my_pid);
        exit(0);
    }

    // --- second fork (parent only) ---
    int pid2 = fork();
    if (pid2 < 0) {
        printf("[multi_fork] FAIL: second fork() returned %d\n", pid2);
        exit(1);
    }
    if (pid2 == 0) {
        int my_pid = getpid();
        if (my_pid == parent_pid) {
            printf("[multi_fork] FAIL child2: same pid as parent (%d)\n", my_pid);
            exit(1);
        }
        printf("[multi_fork] PASS child2: pid = %d\n", my_pid);
        exit(0);
    }

    // --- parent waits for both children ---
    if (pid1 == pid2) {
        printf("[multi_fork] FAIL parent: both forks returned same pid %d\n", pid1);
        exit(1);
    }
    printf("[multi_fork] PASS parent: child pids %d and %d are distinct\n",
           pid1, pid2);

    wait(0);
    wait(0);
    return 0;
}
