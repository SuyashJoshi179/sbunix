// cloexec_test: O_CLOEXEC, fcntl(F_GETFD/F_SETFD), and exec-closes-cloexec.
//
//  1. open(O_CLOEXEC) sets FD_CLOEXEC; fcntl(F_GETFD) reports it.
//  2. plain open() leaves it clear; fcntl(F_SETFD) sets/clears it.
//  3. after fork+exec, the FD_CLOEXEC descriptor is closed in the new
//     image while a plain descriptor survives (checked by cloexec_helper).
//
// /tmp is tmpfs (writable) so the test is self-contained.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static int pass_cnt = 0, fail_cnt = 0;
static void chk(int cond, const char *msg) {
    if (cond) { printf("[cloexec_test] PASS %s\n", msg); pass_cnt++; }
    else      { printf("[cloexec_test] FAIL %s\n", msg); fail_cnt++; }
}

int main(void) {
    const char *pa = "/tmp/cloexec_a.tmp";
    const char *pb = "/tmp/cloexec_b.tmp";
    (void)unlink(pa);
    (void)unlink(pb);

    /* 1. O_CLOEXEC sets the descriptor flag. */
    int fd_cl = open(pa, O_RDWR | O_CREAT | O_CLOEXEC);
    chk(fd_cl >= 0, "open(O_CLOEXEC) succeeds");
    chk(fd_cl >= 0 && fcntl(fd_cl, F_GETFD) == FD_CLOEXEC,
        "F_GETFD reports FD_CLOEXEC after O_CLOEXEC open");

    /* 2. Plain open leaves it clear; F_SETFD toggles it. */
    int fd_kp = open(pb, O_RDWR | O_CREAT);
    chk(fd_kp >= 0, "open() without O_CLOEXEC succeeds");
    chk(fd_kp >= 0 && fcntl(fd_kp, F_GETFD) == 0,
        "F_GETFD reports 0 for a plain fd");
    chk(fcntl(fd_kp, F_SETFD, FD_CLOEXEC) == 0 &&
        fcntl(fd_kp, F_GETFD) == FD_CLOEXEC,
        "F_SETFD sets FD_CLOEXEC");
    chk(fcntl(fd_kp, F_SETFD, 0) == 0 &&
        fcntl(fd_kp, F_GETFD) == 0,
        "F_SETFD clears FD_CLOEXEC");

    /* 3. exec closes the O_CLOEXEC fd, keeps the plain one. fd_kp was
     *    left non-cloexec by step 2. */
    char acl[16], akp[16];
    snprintf(acl, sizeof(acl), "%d", fd_cl);
    snprintf(akp, sizeof(akp), "%d", fd_kp);
    int pid = fork();
    if (pid == 0) {
        char *av[] = { "cloexec_helper", acl, akp, 0 };
        execv("/bin/cloexec_helper", av);
        _exit(127);   /* exec failed */
    }
    chk(pid > 0, "fork before exec");
    int status = 0;
    int w = wait(&status);
    chk(w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "exec'd helper confirms cloexec fd closed, plain fd kept");

    (void)unlink(pa);
    (void)unlink(pb);

    if (fail_cnt == 0)
        printf("cloexec_test: PASS (%d tests)\n", pass_cnt);
    else
        printf("cloexec_test: FAIL (%d/%d failed)\n", fail_cnt,
               pass_cnt + fail_cnt);
    return fail_cnt ? 1 : 0;
}
