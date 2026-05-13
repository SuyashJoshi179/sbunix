/* Smoke-test input for the compile gate. This file MUST compile cleanly
 * (and may even link) against our libc. If the gate reports BUILD-FAIL,
 * the gate or the libc has regressed. */
#include <unistd.h>

int main(void) {
    (void)getpid();
    return 0;
}
