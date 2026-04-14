#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("before fork\n");

    int pid = fork();
    if (pid == 0) {
        printf("child: my pid = %d\n", getpid());
    } else if (pid > 0) {
        printf("parent: child pid = %d, my pid = %d\n", pid, getpid());
    } else {
        printf("fork failed!\n");
    }

    return 0;
}
