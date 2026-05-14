// cloexec_helper <cloexec_fd> <kept_fd>
//
// Exec'd by cloexec_test after fork. Verifies that the descriptor opened
// with O_CLOEXEC was closed across exec, while the plain descriptor
// survived. Exits 0 only when both hold. Not a standalone test — not
// listed in init.c.
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("[cloexec_helper] FAIL bad argc=%d\n", argc);
        return 2;
    }
    int cl = atoi(argv[1]);   // expected CLOSED (opened with O_CLOEXEC)
    int kp = atoi(argv[2]);   // expected OPEN   (plain descriptor)

    int cl_state = fcntl(cl, F_GETFD);   // < 0  => fd is closed
    int kp_state = fcntl(kp, F_GETFD);   // >= 0 => fd is open

    if (cl_state < 0 && kp_state >= 0) {
        printf("[cloexec_helper] PASS cloexec fd closed, plain fd kept\n");
        return 0;
    }
    printf("[cloexec_helper] FAIL cl_state=%d kp_state=%d\n",
           cl_state, kp_state);
    return 1;
}
