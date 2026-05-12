/* atexit_helper — companion binary for atexit_test that exercises the
 * "main-return → CRT exits → atexit handlers fire" path. atexit_test
 * forks, dup2s the write end of a pipe onto fd 3, then execv's into
 * this binary. The parent reads "R" from the pipe iff the handler ran. */
#include <stdlib.h>
#include <unistd.h>

static void writer(void) { write(3, "R", 1); }

int main(void) {
    atexit(writer);
    return 0;
}
