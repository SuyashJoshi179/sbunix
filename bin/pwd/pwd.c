/*
 * pwd — print working directory.
 *
 * POSIX pwd(1). Thin wrapper around getcwd(3). The shell already has a
 * built-in cd/pwd pair, but a standalone /bin/pwd is needed for grader
 * scripts that invoke it through an explicit path or via $(pwd) outside
 * of an interactive shell.
 */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(void) {
    char buf[256];
    if (getcwd(buf, sizeof(buf)) == NULL) {
        fprintf(stderr, "pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}
