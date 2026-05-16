#include <stdio.h>
#include <string.h>

/* Helper used by execve_env_test: scans envp (passed by crt.S in a2) and
 * exits 0 iff EXECVE_TEST_TAG=hello is present. argv is irrelevant. */
int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv;
    if (!envp) return 1;
    for (int i = 0; envp[i]; i++) {
        if (strcmp(envp[i], "EXECVE_TEST_TAG=hello") == 0)
            return 0;
    }
    printf("env_child: tag not found\n");
    return 2;
}
