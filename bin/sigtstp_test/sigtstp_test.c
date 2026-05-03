#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>

int main(void) {
    int pid = fork();
    if (pid == 0) {
        raise(SIGTSTP);
        /* Resumes here on SIGCONT. */
        _exit(0);
    }

    /* Wait for stop. */
    int st = 0;
    int r = waitpid(pid, &st, WUNTRACED);
    if (r != pid || !WIFSTOPPED(st) || WSTOPSIG(st) != SIGTSTP) {
        printf("FAIL  expected WIFSTOPPED+WSTOPSIG=SIGTSTP got r=%d errno=%d st=0x%x\n",
               r, errno, st);
        kill(pid, SIGKILL); waitpid(pid, &st, 0);
        return 1;
    }

    /* Resume. */
    kill(pid, SIGCONT);

    /* Optional WCONTINUED report. */
    r = waitpid(pid, &st, WCONTINUED);
    int saw_cont = (r == pid && WIFCONTINUED(st));

    /* Final exit. */
    while ((r = waitpid(pid, &st, 0)) < 0) {}
    int exited_ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;

    printf("sigtstp_test: stopped=ok cont=%s exited=%s\n",
           saw_cont ? "ok" : "missed", exited_ok ? "ok" : "fail");
    return exited_ok ? 0 : 1;
}
