#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

/* execve must hand envp to the child. Spawn /bin/env_child with a known
 * env entry; verify the child exits 0 (tag seen). */
int main(void) {
    int pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        char *argv[] = { "/bin/env_child", 0 };
        char *envp[] = { "EXECVE_TEST_TAG=hello", "PATH=/bin", 0 };
        execve("/bin/env_child", argv, envp);
        perror("execve");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        perror("waitpid"); return 1;
    }
    if (status != 0) {
        printf("FAIL: child status=%d (expected 0)\n", status);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
