#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && argv[1][0] == 'C') {
        char *p = (char *)malloc(64 * 1024);
        if (!p) { printf("FAIL child malloc\n"); return 1; }
        p[0] = 'X';
        printf("PASS exec_resets_arena (child malloc OK)\n");
        return 0;
    }

    char *parent_buf = (char *)malloc(64 * 1024);
    if (!parent_buf) { printf("FAIL parent malloc\n"); return 1; }
    parent_buf[0] = 'P';

    const char *path = "/bin/exec_resets_arena";
    char *args[] = { (char *)path, "C", 0 };
    execv(path, args);
    printf("FAIL execv returned\n");
    return 1;
}
