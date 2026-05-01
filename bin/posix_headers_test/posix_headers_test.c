#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

int main(void) {
    /* signal.h: sigset_t manipulators */
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGINT);
    if (!sigismember(&s, SIGINT)) { printf("posix_headers_test: sigismember add failed\n"); return 1; }
    sigdelset(&s, SIGINT);
    if (sigismember(&s, SIGINT)) { printf("posix_headers_test: sigismember del failed\n"); return 1; }
    sigfillset(&s);
    if (!sigismember(&s, SIGTERM)) { printf("posix_headers_test: sigfillset failed\n"); return 1; }

    /* fcntl.h: O_EXCL/O_NONBLOCK exist */
    int flags = O_RDONLY | O_EXCL | O_NONBLOCK;
    (void)flags;

    /* errno.h: new constants */
    (void)ENOTIMPL;
    (void)ECONNREFUSED;
    (void)ENOTCONN;
    (void)EHOSTUNREACH;

    /* unistd.h: sleep declared */
    (void)sleep;

    /* stdio.h: getline declared */
    char *line = NULL;
    unsigned long cap = 0;
    (void)line; (void)cap;
    long (*gl)(char **, unsigned long *, FILE *) = getline;
    (void)gl;

    printf("posix_headers_test: ok\n");
    return 0;
}
