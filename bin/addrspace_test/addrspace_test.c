// addrspace_test: verify that fork() gives parent and child independent
// address spaces.  After fork() each process writes a distinct value to a
// shared-looking global; if the write leaks across the fork boundary the
// values will collide and one of the assertions will fail.
//
// Expected output (order may vary):
//   [addrspace] global before fork = 42
//   [addrspace] PASS parent: global = 100 (not 200)
//   [addrspace] PASS child:  global = 200 (not 100)
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int global = 42;

int main(void) {
    printf("[addrspace] global before fork = %d\n", global);

    if (global != 42) {
        printf("[addrspace] FAIL: initial global != 42\n");
        exit(1);
    }

    int rc = fork();
    if (rc < 0) {
        printf("[addrspace] FAIL: fork() returned %d\n", rc);
        exit(1);
    }

    if (rc == 0) {
        // Child: set global to 200
        global = 200;
        if (global != 200) {
            printf("[addrspace] FAIL child: global = %d, expected 200\n", global);
            exit(1);
        }
        printf("[addrspace] PASS child: global = %d (not 100)\n", global);
    } else {
        // Parent: set global to 100
        global = 100;
        if (global != 100) {
            printf("[addrspace] FAIL parent: global = %d, expected 100\n", global);
            exit(1);
        }
        printf("[addrspace] PASS parent: global = %d (not 200)\n", global);
        wait(0);
    }

    return 0;
}
