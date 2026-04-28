#include <errno.h>

/* Single-threaded: one global errno slot. __errno_location() returns a
 * pointer to it so the `errno` macro in <errno.h> is a writable lvalue.
 * No syscall wrapper sets this implicitly today — callers that want
 * POSIX-style `errno` after a -1 return must assign it themselves, or
 * (when MicroPython lands) the port's mphalport.c does the translation. */

#undef errno
int errno;

int *__errno_location(void) {
    return &errno;
}
