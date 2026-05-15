#include <errno.h>

/* Single-threaded: one global errno slot. __errno_location() returns a
 * pointer to it so the `errno` macro in <errno.h> is a writable lvalue.
 * Syscall wrappers translate the kernel's negative-errno return convention
 * to POSIX -1/errno via `syscall_ret` in syscall_priv.h. */

#undef errno
int errno;

int *__errno_location(void) {
    return &errno;
}
