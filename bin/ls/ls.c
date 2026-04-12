#include <unistd.h>
#include <stdio.h>

int main(void) {
    write(STDOUT_FILENO, "ls: listing /\n", 14);
    ls("/");
    write(STDOUT_FILENO, "ls: done\n", 9);
    exit(0);
    return 0;
}
