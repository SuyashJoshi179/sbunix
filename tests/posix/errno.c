/*
 * POSIX conformance test: <errno.h>
 * Reference: docs/susv5-html/basedefs/errno.h.html
 *
 * POSIX requires errno to be a modifiable lvalue (typically a macro
 * expanding to a thread-local accessor). Macros must include the full
 * set of standard error constants.
 */
#include <errno.h>

__attribute__((unused))
static void _errno_lvalue(void) {
    errno = 0;
    int e = errno;
    (void)e;
}

__attribute__((unused))
static void _macro_checks(void) {
    int x = 0;
    x |= EPERM | ENOENT | ESRCH | EINTR | EIO | ENXIO | E2BIG | ENOEXEC;
    x |= EBADF | ECHILD | EAGAIN | ENOMEM | EACCES | EFAULT | EBUSY;
    x |= EEXIST | EXDEV | ENODEV | ENOTDIR | EISDIR | EINVAL | ENFILE;
    x |= EMFILE | ENOTTY | ETXTBSY | EFBIG | ENOSPC | ESPIPE | EROFS;
    x |= EMLINK | EPIPE | EDOM | ERANGE | EDEADLK | ENOLCK | ENAMETOOLONG;
    x |= EWOULDBLOCK;
    (void)x;
}
