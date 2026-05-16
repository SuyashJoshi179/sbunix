#include <stdio.h>
#include <stdlib.h>

/* Tiny self-identifying binary used by execvp_path_test. Exits with the
 * sentinel value 42 to confirm the PATH walker found us, and prints
 * argv[1] if present so the parent can verify argv propagation. */
int main(int argc, char **argv) {
    if (argc > 1) printf("execvp_helper: %s\n", argv[1]);
    return 42;
}
