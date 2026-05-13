/* Smoke-test input for the compile gate. This file MUST fail to compile
 * against our libc because <aio.h> is intentionally out of scope.
 * If the gate reports BUILD-OK for this file, the gate is broken. */
#include <aio.h>

int main(void) { return 0; }
